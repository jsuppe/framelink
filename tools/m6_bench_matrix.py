#!/usr/bin/env python3
"""Run M6 session benchmark cells (fl_gst_av_bench) and collect their summaries.

Desktop: runs the benchmark from a build directory with the built plugin.
Android: pushes the statically linked benchmark with adb and runs it on the
device. Cells run one at a time; each writes <label>.summary.txt, and the
combined results go to --out as JSON (one object per cell).

Cell sets:
  baseline  the M6.1 baseline matrix (see docs/m6/QUALIFICATION_MATRIX.md)
  soak      the M6.2 sustained cell (use --seconds 1800)

Examples:
  tools/m6_bench_matrix.py --build build --set baseline --platform linux --out base.json
  tools/m6_bench_matrix.py --build build --config Release --platform windows --out base.json
  tools/m6_bench_matrix.py --build ../fl-arm64 --adb SERIAL --platform android --out base.json
"""
import argparse, json, os, platform, subprocess, sys, tempfile, time, uuid

# name: benchmark options. "ref" is the reference cell every comparison uses.
SETS = {
    "baseline": [
        ("ref",          ["--size", "320x180", "--format", "RGBA", "--mode", "bin"]),
        ("element",      ["--size", "320x180", "--format", "RGBA", "--mode", "element"]),
        ("nv12",         ["--size", "320x180", "--format", "NV12", "--mode", "bin"]),
        ("native",       ["--size", "320x180", "--format", "RGBA", "--mode", "bin", "--native"]),
        ("cpu-640",      ["--size", "640x360", "--format", "RGBA", "--mode", "bin"]),
        ("native-720p",  ["--size", "1280x720", "--format", "RGBA", "--mode", "bin", "--native"]),
        ("cpu-720p",     ["--size", "1280x720", "--format", "RGBA", "--mode", "bin"]),
        ("ref-traced",   ["--size", "320x180", "--format", "RGBA", "--mode", "bin", "--trace"]),
    ],
    "soak": [
        ("soak-ref",     ["--size", "320x180", "--format", "RGBA", "--mode", "bin"]),
    ],
}
# Android production evidence is documented at 160x90; add it as a cell there.
ANDROID_EXTRA = [("ref-160", ["--size", "160x90", "--format", "RGBA", "--mode", "bin"])]

def parse_summary(text):
    out = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out

def run_desktop(args, cells):
    exe_dir = os.path.join(args.build, args.config) if args.config else args.build
    exe = os.path.join(exe_dir, "fl_gst_av_bench" + (".exe" if os.name == "nt" else ""))
    plugin = os.path.join(exe_dir, "gstframelink.dll" if os.name == "nt" else "libgstframelink.so")
    out_dir = args.evidence or tempfile.mkdtemp(prefix="fl-m6-bench-")
    os.makedirs(out_dir, exist_ok=True)
    env = dict(os.environ, G_DEBUG="fatal-warnings")
    results = []
    for label, opts in cells:
        cmd = [exe, plugin, "--seconds", str(args.seconds), "--fps", str(args.fps), "--out", out_dir, "--label", label] + opts
        results.append(run_cell(label, opts, lambda: subprocess.run(cmd, capture_output=True, text=True, env=env,
                                                                      timeout=args.seconds + 180)))
    return results

def run_android(args, cells):
    adb = ["adb", "-s", args.adb]
    remote = args.remote_dir
    subprocess.run(adb + ["shell", f"mkdir -p {remote}"], check=True)
    subprocess.run(adb + ["push", os.path.join(args.build, "fl_gst_av_bench"), f"{remote}/fl_gst_av_bench"], check=True,
                   capture_output=True)
    results = []
    for label, opts in cells:
        scratch = f"{remote}/m6-bench-{uuid.uuid4().hex[:12]}"
        shell = (f"mkdir -p {scratch} && chmod 700 {scratch} && cd {remote} && "
                 f"export LD_LIBRARY_PATH=. TMPDIR={scratch} G_DEBUG=fatal-warnings && "
                 f"./fl_gst_av_bench static --seconds {args.seconds} --fps {args.fps} --out {scratch} --label {label} "
                 f"{' '.join(opts)}; rc=$?; rm -rf {scratch}; exit $rc")
        results.append(run_cell(label, opts, lambda: subprocess.run(adb + ["shell", shell], capture_output=True, text=True,
                                                                      timeout=args.seconds + 240)))
    return results

def run_cell(label, opts, launch):
    begin = time.monotonic()
    try:
        proc = launch()
        rc, text = proc.returncode, proc.stdout + proc.stderr
    except subprocess.TimeoutExpired as e:
        rc, text = -1, f"timeout: {e}"
    summary = parse_summary(text)
    record = {"cell": label, "options": opts, "exit": rc, "seconds": round(time.monotonic() - begin, 1),
              "summary": summary}
    if rc != 0:
        record["output"] = text[-4000:]
    skipped = text.startswith("SKIP")
    state = "SKIP" if skipped else ("PASS" if rc == 0 else "FAIL")
    print(state, label, summary.get("config", ""), f"{record['seconds']}s", flush=True)
    for key in ("consumer.video.arrival.ms", "consumer.video.render_jitter.p1_p99.ms", "consumer.av_offset.abs.ms",
                "consumer.video.deadline_misses", "consumer.cpu.percent", "producer.cpu.percent"):
        if key in summary:
            print(f"  {key}={summary[key]}", flush=True)
    if rc != 0 and not skipped:
        sys.stderr.write(text[-2000:] + "\n")
    return record

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", required=True)
    ap.add_argument("--config", help="multi-config build subdirectory (Windows: Release)")
    ap.add_argument("--set", default="baseline", choices=sorted(SETS))
    ap.add_argument("--cells", help="comma-separated subset of the set's cells")
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--repeat", type=int, default=1, help="rounds; each round runs every cell once (interleaved)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--platform", help="label stored with the results")
    ap.add_argument("--adb", help="Android device serial (Android build directory)")
    ap.add_argument("--remote-dir", default="/data/local/tmp/framelink-m6")
    ap.add_argument("--evidence", help="desktop: directory for per-cell summaries (default: a temporary one)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    cells = list(SETS[args.set]) + (ANDROID_EXTRA if args.adb and args.set == "baseline" else [])
    if args.cells:
        wanted = args.cells.split(",")
        cells = [c for c in cells if c[0] in wanted]
    if args.repeat > 1:
        cells = [(f"{label}-r{i + 1}", opts) for i in range(args.repeat) for label, opts in cells]
    started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    results = run_android(args, cells) if args.adb else run_desktop(args, cells)
    doc = {"platform": args.platform or ("android:" + args.adb if args.adb else platform.platform()),
           "set": args.set, "repeat": args.repeat, "seconds": args.seconds, "fps": args.fps,
           "started": started, "cells": results}
    with open(args.out, "w") as f:
        json.dump(doc, f, indent=1)
    failed = [r["cell"] for r in results if r["exit"] != 0]
    print("cells", len(results), "failed", failed or "none", flush=True)
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
