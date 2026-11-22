#!/usr/bin/env python3
"""Run an Android build's ctest suite on a connected device over adb.

Plain ctest runs the cross-compiled binaries on the host, so they fail. This
reads the registered tests from the build directory, pushes their executables
to DEVICE_DIR, rewrites build-directory paths to DEVICE_DIR, and runs each test
on the device with its ENVIRONMENT, TIMEOUT, SKIP_RETURN_CODE and WILL_FAIL.
The two-process round trip (run_roundtrip.cmake) runs its producer and consumer
concurrently, as the CMake script does; the consumer's exit code is the verdict.

  python3 tools/run_android_ctest.py build-android /data/local/tmp/fl-suite
  python3 tools/run_android_ctest.py build-android /data/local/tmp/fl-suite \\
      -R '^gst_relay_.*_android$' --rounds 5 --no-push

Prints PASS/FAIL/SKIP per test (failures with the tail of their device log,
kept in DEVICE_DIR/logs) and a TOTAL line; exits 1 if any test failed.
Tests run one at a time. Set ANDROID_SERIAL to pick a device.
"""
import argparse, json, os, re, shlex, subprocess, sys, time


def adb(*args):
    return subprocess.run(["adb", *args], capture_output=True, text=True)


def props(test):
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def roundtrip_args(command):
    """-D definitions of a run_roundtrip.cmake test, or None for other tests."""
    if os.path.basename(command[0]) != "cmake" or not any(a.endswith("run_roundtrip.cmake") for a in command):
        return None
    return dict(a[2:].split("=", 1) for a in command if a.startswith("-D"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("build", help="Android CMake build directory")
    ap.add_argument("remote", help="scratch directory on the device (it is replaced unless --no-push)")
    ap.add_argument("-R", help="only tests whose name matches this regex")
    ap.add_argument("-E", help="exclude tests whose name matches this regex")
    ap.add_argument("--rounds", type=int, default=1)
    ap.add_argument("--no-push", action="store_true", help="reuse executables already in REMOTE")
    a = ap.parse_args()
    build, remote = os.path.realpath(a.build), a.remote.rstrip("/")

    listing = subprocess.run(["ctest", "--show-only=json-v1"], cwd=build, capture_output=True, text=True, check=True)
    tests = [t for t in json.loads(listing.stdout)["tests"] if "command" in t]
    if a.R: tests = [t for t in tests if re.search(a.R, t["name"])]
    if a.E: tests = [t for t in tests if not re.search(a.E, t["name"])]
    if not tests: sys.exit("no tests selected")

    def remap(s):
        return s.replace(build, remote)

    def device_command(test):
        rt = roundtrip_args(test["command"])
        if rt is not None:
            def side(k):
                return " ".join([remap(rt[k])] + [shlex.quote(x) for x in rt[k + "_ARGS"].split(";")])
            return f"( {side('PRODUCER')} & ) ; {side('CONSUMER')}", 90
        p = props(test)
        env = " ".join(shlex.quote(remap(e)) for e in p.get("ENVIRONMENT", []))
        cmd = " ".join(shlex.quote(remap(x)) for x in test["command"])
        return (f"env {env} {cmd}" if env else cmd), p.get("TIMEOUT", 1500)

    if not a.no_push:
        exes = set()
        for t in tests:
            rt = roundtrip_args(t["command"])
            if rt is not None: exes |= {rt["PRODUCER"], rt["CONSUMER"]}
            elif t["command"][0].startswith(build + "/"): exes.add(t["command"][0])
        adb("shell", f"rm -rf {shlex.quote(remote)} && mkdir -p {shlex.quote(remote)}/logs")
        r = adb("push", *sorted(exes), remote + "/")
        if r.returncode: sys.exit("adb push failed: " + r.stderr)
        # The gst tests take the plugin path as argv[1]; on Android it is linked
        # statically and the argument is only a placeholder, but keep it present.
        adb("shell", f"touch {shlex.quote(remote)}/libgstframelink.a")

    totals = {"PASS": 0, "FAIL": 0, "SKIP": 0}; failed = []
    for rnd in range(a.rounds):
        for t in tests:
            name, p = t["name"], props(t)
            cmd, timeout = device_command(t)
            log = f"{remote}/logs/{name}.log"
            sh = f"cd {shlex.quote(remote)} && timeout {int(timeout)} sh -c {shlex.quote(cmd)} >{shlex.quote(log)} 2>&1; echo RC=$?"
            start = time.time(); r = adb("shell", sh); took = time.time() - start
            m = re.search(r"RC=(\d+)", r.stdout); rc = int(m.group(1)) if m else -1
            ok = (rc != 0) if p.get("WILL_FAIL") else (rc == 0)
            skip = p.get("SKIP_RETURN_CODE")
            status = "SKIP" if skip is not None and rc == skip else ("PASS" if ok else "FAIL")
            totals[status] += 1
            line = f"{status} {name} {took:.1f}s"
            if status != "PASS": line += f" rc={rc}"
            if a.rounds > 1: line += f" round={rnd + 1}"
            print(line, flush=True)
            if status == "FAIL":
                failed.append(name)
                tail = adb("shell", f"tail -n 15 {shlex.quote(log)}").stdout.rstrip("\n")
                print("  | " + tail.replace("\n", "\n  | "), flush=True)
    print(f"TOTAL pass={totals['PASS']} fail={totals['FAIL']} skip={totals['SKIP']} of {sum(totals.values())}")
    if failed: print("FAILED: " + " ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
