#!/usr/bin/env python3
"""Two-process qualification of samples/gst_av_example.cpp (M5b task7).

Each case launches a real producer and consumer process on a fresh private
session directory and requires the consumer's headless verification to pass.
A negative case requires disagreeing track sets to fail on both sides.
With --adb SERIAL --remote-dir DIR both processes run on an Android device
(statically linked example already pushed to DIR); each case gets a fresh
remote scratch directory that is removed afterwards.
"""
import argparse, json, os, shlex, subprocess, sys, tempfile, time, uuid

CASES = [
    # name, tracks, format, consumer first
    ("av-rgba-consumer-first", "av", "RGBA", True),
    ("av-rgba-producer-first", "av", "RGBA", False),
    ("av-nv12", "av", "NV12", True),
    ("video-bgra", "video", "BGRA", True),
    ("audio-only", "audio", "RGBA", False),
]

class LocalRoot:
    def __init__(self, args):
        self.tmp = tempfile.TemporaryDirectory(prefix="fl-m5b-example-")
        self.path, self.example = self.tmp.name, args.example
    def command(self, argv):
        return [self.example] + argv
    def close(self):
        self.tmp.cleanup()

class AdbRoot:
    def __init__(self, args):
        self.adb, self.remote = ["adb", "-s", args.adb], args.remote_dir
        self.path = f"{self.remote}/m5b-example-{uuid.uuid4().hex[:12]}"
        subprocess.run(self.adb + ["shell", f"mkdir -p {self.path} && chmod 700 {self.path}"], check=True)
    def command(self, argv):
        inner = " ".join(shlex.quote(a) for a in argv)
        return self.adb + ["shell", f"cd {self.remote} && export LD_LIBRARY_PATH=. TMPDIR={self.path} "
                           f"G_DEBUG=fatal-warnings && ./framelink_gst_av {inner}"]
    def close(self):
        # "[f]ramelink" never matches this cleanup shell's own command line.
        subprocess.run(self.adb + ["shell", f"rm -rf {self.path}; pkill -f '[f]ramelink_gst_av.*{self.path}'; true"])

def run_case(args, env, name, tracks, fmt, consumer_first, consumer_tracks=None, expect_pass=True):
    root = AdbRoot(args) if args.adb else LocalRoot(args)
    try:
        return run_case_in(root, args, env, name, tracks, fmt, consumer_first, consumer_tracks, expect_pass)
    finally:
        root.close()

def run_case_in(root, args, env, name, tracks, fmt, consumer_first, consumer_tracks, expect_pass):
    session = root.path + "/session"
    common = ["--session", session, "--format", fmt, "--size", "160x90", "--fps", "30",
              "--seconds", str(args.seconds), "--timeout-ms", "8000"]
    producer = root.command(["producer", "--tracks", tracks] + common)
    consumer = root.command(["consumer", "--tracks", consumer_tracks or tracks, "--verify",
                             "--latency-ms", "150", "--pool", "8"] + common)
    order = [consumer, producer] if consumer_first else [producer, consumer]
    role = {id(consumer): "consumer", id(producer): "producer"}
    started = time.monotonic()
    procs = []
    for i, cmd in enumerate(order):
        procs.append(subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
        if i == 0:
            time.sleep(0.3)
    outputs = {}
    for cmd, proc in zip(order, procs):
        try:
            out, err = proc.communicate(timeout=args.seconds + 30)
        except subprocess.TimeoutExpired:
            for p in procs:
                p.kill()
            print(f"FAIL {name}: timeout", flush=True)
            return False
        outputs[role[id(cmd)]] = (proc.returncode, out, err)
    elapsed = time.monotonic() - started
    (pc, _, perr), (cc, cout, cerr) = outputs["producer"], outputs["consumer"]
    verify = None
    for line in cout.splitlines():
        if line.startswith("{"):
            verify = json.loads(line)
    if expect_pass:
        ok = pc == 0 and cc == 0 and verify and verify.get("result") == "pass"
    else:
        ok = pc != 0 and cc != 0 and elapsed < args.seconds + 20
    print(("PASS" if ok else "FAIL"), name, f"producer={pc} consumer={cc} elapsed={elapsed:.2f}s",
          json.dumps(verify) if verify else "", flush=True)
    if not ok:
        sys.stderr.write(f"--- producer stderr\n{perr}\n--- consumer stderr\n{cerr}\n")
    return ok

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--example", help="framelink_gst_av executable (desktop)")
    ap.add_argument("--adb", help="Android device serial; run both roles on the device")
    ap.add_argument("--remote-dir", help="device directory holding framelink_gst_av (with --adb)")
    ap.add_argument("--plugin", help="gstframelink plugin file (desktop)")
    ap.add_argument("--seconds", type=int, default=3)
    ap.add_argument("--repeat", type=int, default=1)
    args = ap.parse_args()
    if bool(args.adb) == bool(args.example) or (args.adb and not args.remote_dir):
        ap.error("use either --example, or --adb with --remote-dir")
    env = dict(os.environ)
    if args.plugin:
        plugin_dir = os.path.dirname(os.path.abspath(args.plugin))
        env["GST_PLUGIN_PATH"] = plugin_dir + os.pathsep + env.get("GST_PLUGIN_PATH", "")
        if os.name == "nt":
            # The registry scanner loads the plugin in another process; its
            # FrameLink DLL dependencies must be resolvable from PATH.
            env["PATH"] = plugin_dir + os.pathsep + env.get("PATH", "")
    env.setdefault("G_DEBUG", "fatal-warnings")
    results = []
    for _ in range(args.repeat):
        for case in CASES:
            results.append(run_case(args, env, *case))
        results.append(run_case(args, env, "negative-track-mismatch", "av", "RGBA", True,
                                consumer_tracks="audio", expect_pass=False))
    passed = sum(results)
    print(f"m5b gst A/V example: {passed}/{len(results)} cases passed", flush=True)
    return 0 if passed == len(results) else 1

if __name__ == "__main__":
    sys.exit(main())
