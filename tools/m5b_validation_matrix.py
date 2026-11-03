#!/usr/bin/env python3
"""M5b task8 automated validation matrix.

Runs two suites with repeats and writes a JSON summary:
  m5b_av                public A/V elements, coordinator, timing, lifecycle,
                        task8 validation and the two-process example;
  gst_video_regression  pre-M5b video-only framelinksink/framelinksrc behaviour.

Desktop (Linux/Windows): suites are the CTest labels of the same names. Each
test runs alone (serially: several M5/broker tests share endpoints) with
`--repeat until-fail:N`.
Android: the statically linked binaries are pushed with adb and each run gets a
fresh private TMPDIR that is removed afterwards; the two-process example runs
through test/m5b_gst_av_example.py --adb.

A suite passes only if every registered test passes every repeat. Tests a
platform does not build (for example Windows-only native D3D tests on Linux)
are reported as not registered, never as passed.
"""
import argparse, json, os, platform, subprocess, sys, time, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SUITES = ("m5b_av", "gst_video_regression")
# Desktop builds must register every m5b_av test; absence is a matrix failure.
M5B_AV_REQUIRED = {"gst_session_transport", "gst_session_context", "gst_audio_sink", "gst_audio_source",
                   "gst_event_channel", "gst_local_audio", "gst_av_timeline", "gst_av_validation",
                   "gst_local_av", "gst_av_bin", "gst_av_src_bin", "gst_av_parity", "gst_av_qualification",
                   "m5b_gst_av_example"}
# Android: (name, binary, args). "static" fills the plugin-path argument that
# desktop builds load; Android registers the plugins statically.
ANDROID = {
    "m5b_av": [
        ("gst_session_transport", "fl_gst_session_transport", []),
        ("gst_session_context", "fl_gst_session_context", []),
        ("gst_audio_sink", "fl_gst_audio_sink", ["static"]),
        ("gst_audio_source", "fl_gst_audio_source", ["static"]),
        ("gst_event_channel", "fl_gst_event_channel", []),
        ("gst_local_audio", "fl_gst_local_audio", []),
        ("gst_av_timeline", "fl_gst_av_timeline", ["static"]),
        ("gst_av_validation", "fl_gst_av_validation", ["static"]),
        ("gst_local_av", "fl_gst_local_av", ["static"]),
        ("gst_av_bin", "fl_gst_av_bin", ["static"]),
        ("gst_av_src_bin", "fl_gst_av_src_bin", ["static"]),
        ("gst_av_parity", "fl_gst_av_parity", ["static"]),
        ("gst_av_qualification", "fl_gst_av_qualification", ["static"]),
    ],
    "gst_video_regression": [
        ("gst_native_source_android", "fl_gst_native_source_android", []),
        ("gst_native_producer_android", "fl_gst_native_producer_android", []),
        ("gst_nv12_roundtrip", "fl_gst_nv12", ["static"]),
        ("gst_rgb10a2_roundtrip", "fl_gst_nv12", ["static", "rgb10a2"]),
        ("gst_timestamp_contract", "fl_gst_timestamps", ["static"]),
        ("gst_pool_policy", "fl_gst_pool_policy", ["static"]),
        ("gst_policy_generation", "fl_gst_policy_generation", ["static"]),
    ],
}

def tail(text, lines=40):
    return "\n".join(text.splitlines()[-lines:])

def ctest_names(args, label):
    cmd = ["ctest", "--test-dir", args.build, "--show-only=json-v1", "-L", f"^{label}$"]
    if args.config:
        cmd += ["-C", args.config]
    data = json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout)
    return [t["name"] for t in data["tests"]]

def run_desktop(args):
    results = {}
    for suite in args.suites:
        names = ctest_names(args, suite)
        entry = {"registered": names, "tests": []}
        if suite == "m5b_av":
            entry["missing"] = sorted(M5B_AV_REQUIRED - set(names))
        for name in names:
            cmd = ["ctest", "--test-dir", args.build, "-R", f"^{name}$", "--repeat", f"until-fail:{args.repeat}",
                   "--output-on-failure"]
            if args.config:
                cmd += ["-C", args.config]
            begin = time.monotonic()
            proc = subprocess.run(cmd, capture_output=True, text=True)
            ok = proc.returncode == 0
            record = {"name": name, "repeat": args.repeat, "result": "pass" if ok else "fail",
                      "seconds": round(time.monotonic() - begin, 2)}
            print(("PASS" if ok else "FAIL"), suite, name, f"x{args.repeat}", f"{record['seconds']}s", flush=True)
            if not ok:
                record["output"] = tail(proc.stdout + proc.stderr)
                sys.stderr.write(record["output"] + "\n")
            entry["tests"].append(record)
        results[suite] = entry
    return results

def run_android(args):
    adb = ["adb", "-s", args.adb]
    binaries = sorted({b for suite in args.suites for _, b, _ in ANDROID[suite]}
                      | ({"framelink_gst_av"} if "m5b_av" in args.suites else set()))
    subprocess.run(adb + ["shell", f"mkdir -p {args.remote_dir}"], check=True)
    for b in binaries:
        subprocess.run(adb + ["push", os.path.join(args.build, b), f"{args.remote_dir}/{b}"], check=True,
                       capture_output=True)
    results = {}
    for suite in args.suites:
        entry = {"registered": [n for n, _, _ in ANDROID[suite]], "tests": []}
        cases = list(ANDROID[suite])
        if suite == "m5b_av":
            cases.append(("m5b_gst_av_example", None, None))
        for name, binary, argv in cases:
            begin, passed, output = time.monotonic(), 0, ""
            if binary is None:
                proc = subprocess.run([sys.executable, os.path.join(HERE, "..", "test", "m5b_gst_av_example.py"),
                                       "--adb", args.adb, "--remote-dir", args.remote_dir,
                                       "--repeat", str(args.repeat)], capture_output=True, text=True)
                ok = proc.returncode == 0
                passed = args.repeat if ok else 0
                output = proc.stdout + proc.stderr
            else:
                for _ in range(args.repeat):
                    scratch = f"{args.remote_dir}/m5b-run-{uuid.uuid4().hex[:12]}"
                    shell = (f"mkdir -p {scratch} && chmod 700 {scratch} && cd {args.remote_dir} && "
                             f"export LD_LIBRARY_PATH=. TMPDIR={scratch} G_DEBUG=fatal-warnings && "
                             f"./{binary} {' '.join(argv)}; rc=$?; rm -rf {scratch}; exit $rc")
                    proc = subprocess.run(adb + ["shell", shell], capture_output=True, text=True,
                                          timeout=args.android_timeout)
                    if proc.returncode != 0:
                        output = proc.stdout + proc.stderr
                        break
                    passed += 1
                ok = passed == args.repeat
            record = {"name": name, "repeat": args.repeat, "passed": passed, "result": "pass" if ok else "fail",
                      "seconds": round(time.monotonic() - begin, 2)}
            print(("PASS" if ok else "FAIL"), suite, name, f"{passed}/{args.repeat}", f"{record['seconds']}s", flush=True)
            if not ok:
                record["output"] = tail(output)
                sys.stderr.write(record["output"] + "\n")
            entry["tests"].append(record)
        results[suite] = entry
    return results

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", required=True, help="CMake build directory")
    ap.add_argument("--config", help="multi-config build type (Windows: Release)")
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--suites", default=",".join(SUITES))
    ap.add_argument("--adb", help="Android device serial (Android build directory)")
    ap.add_argument("--remote-dir", default="/data/local/tmp/fl-m5b-matrix")
    ap.add_argument("--android-timeout", type=int, default=400)
    ap.add_argument("--platform", help="label recorded in the summary")
    ap.add_argument("--out", help="write the JSON summary here")
    args = ap.parse_args()
    args.suites = [s for s in args.suites.split(",") if s]
    for s in args.suites:
        if s not in SUITES:
            ap.error(f"unknown suite {s}")
    results = run_android(args) if args.adb else run_desktop(args)
    ok = all(t["result"] == "pass" for r in results.values() for t in r["tests"]) and \
         all(r["tests"] for r in results.values()) and not any(r.get("missing") for r in results.values())
    summary = {"platform": args.platform or ("android:" + args.adb if args.adb else platform.platform()),
               "repeat": args.repeat, "suites": results, "result": "pass" if ok else "fail"}
    for suite, r in results.items():
        good = sum(t["result"] == "pass" for t in r["tests"])
        missing = f", missing {r['missing']}" if r.get("missing") else ""
        print(f"{suite}: {good}/{len(r['tests'])} tests passed x{args.repeat}{missing}", flush=True)
    if args.out:
        with open(args.out, "w") as f:
            json.dump(summary, f, indent=1)
    print("m5b validation matrix:", summary["result"], flush=True)
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
