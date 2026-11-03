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
import argparse, csv, json, os, platform, re, shutil, statistics, subprocess, sys, tempfile, threading, time, uuid

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
    # Project targets: 1080p60 and 4K30, BGRA and NV12, native and CPU consumer;
    # "src-*" measure the test sources alone (no FrameLink) for the producer.
    "requirement": [
        (f"{size}-{fmt.lower()}-{path}", ["--size", dims, "--fps", fps, "--format", fmt, "--mode", "bin"] + (["--native"] if path == "native" else []))
        for size, dims, fps in (("1080p60", "1920x1080", "60"), ("4k30", "3840x2160", "30"))
        for fmt in ("BGRA", "NV12") for path in ("native", "cpu")
    ] + [
        (f"src-{size}-{fmt.lower()}", ["--size", dims, "--fps", fps, "--format", fmt, "--source-only"])
        for size, dims, fps in (("1080p60", "1920x1080", "60"), ("4k30", "3840x2160", "30")) for fmt in ("BGRA", "NV12")
    ],
    "soak": [
        ("soak-ref",     ["--size", "320x180", "--format", "RGBA", "--mode", "bin"]),
    ],
}
# Android production evidence is documented at 160x90; add it as a cell there.
# Android native consumer memory supports RGBA layouts only (not BGRA or NV12),
# so the requirement set adds RGBA native cells there.
ANDROID_EXTRA = {
    "baseline": [("ref-160", ["--size", "160x90", "--format", "RGBA", "--mode", "bin"])],
    "requirement": [(f"{size}-rgba-native", ["--size", dims, "--fps", fps, "--format", "RGBA", "--mode", "bin", "--native"])
                    for size, dims, fps in (("1080p60", "1920x1080", "60"), ("4k30", "3840x2160", "30"))]
                   + [(f"src-{size}-rgba", ["--size", dims, "--fps", fps, "--format", "RGBA", "--source-only"])
                      for size, dims, fps in (("1080p60", "1920x1080", "60"), ("4k30", "3840x2160", "30"))],
}

def parse_summary(text):
    out = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out

# ---- GPU samplers -------------------------------------------------------------
# Device-wide NVIDIA counters (Linux, Windows): GBM/DMABuf rings on Linux are
# not attributed to a process by the driver, so only device totals exist; an
# idle reference before each cell is reported alongside.
IDLE_S = 8
class NvidiaDevice:
    def __init__(self):
        self.rows, self.header = [], None
        self.proc = subprocess.Popen(["nvidia-smi", "dmon", "-s", "puctm", "-d", "1"], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True)
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()
    def _read(self):
        for line in self.proc.stdout:
            parts = line.split()
            if line.startswith("#") and self.header is None:
                self.header = parts[1:]
            elif parts and not line.startswith("#") and self.header:
                self.rows.append((time.monotonic(), dict(zip(self.header, parts))))
    def stop(self):
        self.proc.terminate()
        self.proc.wait()
    def summary(self, idle, active):
        out = {}
        for name, (a, b) in (("idle", idle), ("active", active)):
            rows = [r for t, r in self.rows if a <= t <= b]
            for field, key in (("pwr", "power_w"), ("sm", "sm_pct"), ("mem", "memctl_pct"), ("pclk", "core_mhz"),
                               ("mclk", "mem_mhz"), ("fb", "fb_mb"), ("rxpci", "pcie_rx_mb_s"), ("txpci", "pcie_tx_mb_s")):
                vals = [float(r[field]) for r in rows if r.get(field, "-").replace(".", "", 1).isdigit()]
                if vals:
                    out[f"gpu.device.{name}.{key}"] = f"{statistics.mean(vals):.1f}"
            out[f"gpu.device.{name}.samples"] = str(len(rows))
        return out

def have_nvidia():
    return shutil.which("nvidia-smi") is not None

# Windows per-process GPU counters (engine utilization, dedicated/shared memory).
class WindowsProcessGpu:
    COUNTERS = [r"\GPU Engine(*)\Utilization Percentage", r"\GPU Process Memory(*)\Dedicated Usage",
                r"\GPU Process Memory(*)\Shared Usage"]
    def __init__(self, out_dir, label):
        self.csv = os.path.join(out_dir, f"{label}-gpu-counters.csv")
        self.proc = None
    def start(self, samples):
        if os.path.exists(self.csv):
            os.remove(self.csv)
        self.proc = subprocess.Popen(["typeperf"] + self.COUNTERS + ["-si", "1", "-sc", str(samples), "-f", "CSV", "-o", self.csv, "-y"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    def stop(self):
        if self.proc:
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
    def summary(self, pids):
        out = {}
        if not os.path.exists(self.csv):
            return {"gpu.process": "no counter samples"}
        with open(self.csv, newline="", encoding="utf-8", errors="replace") as f:
            rows = list(csv.reader(f))
        if len(rows) < 2:
            return {"gpu.process": "no counter samples"}
        header, data = rows[0], rows[1:]
        for side, pid in pids.items():
            tag = f"pid_{pid}_"
            engines, dedicated, shared = {}, [], []
            for col, name in enumerate(header):
                if tag not in name:
                    continue
                vals = [float(r[col]) for r in data if col < len(r) and r[col].strip() not in ("", " ")]
                if "Utilization Percentage" in name:
                    m = re.search(r"engtype_([^)\\]+)", name)
                    engines.setdefault(m.group(1) if m else "?", []).append(vals)
                elif "Dedicated Usage" in name:
                    dedicated.append(vals)
                elif "Shared Usage" in name:
                    shared.append(vals)
            per_type = {}
            for etype, series in engines.items():
                n = min(len(v) for v in series) if series else 0
                if n:
                    per_type[etype] = statistics.mean(sum(v[i] for v in series) for i in range(n))
            for etype, mean in sorted(per_type.items()):
                if mean >= 0.01:
                    out[f"gpu.{side}.engine.{etype}_pct"] = f"{mean:.2f}"
            out[f"gpu.{side}.busiest_engine_pct"] = f"{max(per_type.values()):.2f}" if per_type else "0.00"
            for key, series in (("dedicated_mb", dedicated), ("shared_mb", shared)):
                vals = [statistics.mean(v) for v in series if v]
                out[f"gpu.{side}.{key}"] = f"{sum(vals) / 2**20:.1f}" if vals else "0.0"
        return out

# Android: GPU active time per uid from `dumpsys gpu` (the benchmark runs as shell, uid 2000).
def android_gpu_sample(adb):
    proc = subprocess.run(adb + ["shell", "date +%s%N; dumpsys gpu"], capture_output=True, text=True, timeout=30)
    lines = proc.stdout.splitlines()
    try:
        now = int(lines[0].strip())
    except (IndexError, ValueError):
        return None
    for line in lines:
        parts = line.split()
        if len(parts) == 4 and parts[1] == "2000" and parts[2].isdigit():
            return now, int(parts[2])
    return now, 0

# ---- cells -----------------------------------------------------------------------
def stream(cmd, timeout, on_pids):
    """Run cmd, calling on_pids({'producer': pid, 'consumer': pid}) when the benchmark reports them."""
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=CHILD_ENV)
    lines = []
    def read():
        for line in proc.stdout:
            lines.append(line)
            m = re.match(r"pids producer=(\d+) consumer=(\d+)", line)
            if m:
                on_pids({"producer": int(m.group(1)), "consumer": int(m.group(2))})
    t = threading.Thread(target=read, daemon=True)
    t.start()
    try:
        rc = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        rc = -1
        lines.append("timeout\n")
    t.join(timeout=10)
    return rc, "".join(lines)

CHILD_ENV = dict(os.environ, G_DEBUG="fatal-warnings")

def run_desktop(args, cells):
    exe_dir = os.path.join(args.build, args.config) if args.config else args.build
    exe = os.path.join(exe_dir, "fl_gst_av_bench" + (".exe" if os.name == "nt" else ""))
    plugin = os.path.join(exe_dir, "gstframelink.dll" if os.name == "nt" else "libgstframelink.so")
    out_dir = args.evidence or tempfile.mkdtemp(prefix="fl-m6-bench-")
    os.makedirs(out_dir, exist_ok=True)
    results = []
    for label, opts in cells:
        cmd = [exe, plugin, "--seconds", str(args.seconds), "--fps", str(args.fps), "--out", out_dir, "--label", label] + opts
        device = NvidiaDevice() if have_nvidia() else None
        if device:
            time.sleep(IDLE_S)
        begin = time.monotonic()
        state = {}
        per_process = WindowsProcessGpu(out_dir, label) if os.name == "nt" else None
        def on_pids(pids):
            state["pids"], state["t0"] = pids, time.monotonic()
            if per_process:
                # Instances are expanded when typeperf starts: wait until both
                # processes have created their GPU devices.
                time.sleep(3)
                per_process.start(max(1, args.seconds - 5))
        rc, text = stream(cmd, args.seconds + 180, on_pids)
        end = time.monotonic()
        gpu = {}
        if device:
            device.stop()
            t0 = state.get("t0", begin)
            gpu.update(device.summary((begin - IDLE_S + 1, begin), (t0 + 3, end - 2)))
        if per_process:
            per_process.stop()
            if "pids" in state:
                gpu.update(per_process.summary(state["pids"]))
        if not device and not per_process:
            gpu["gpu"] = "no GPU counters available"
        results.append(record_cell(label, opts, rc, text, end - begin, gpu))
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
        samples = []
        def sampler(pids):
            # Steady state: after warm-up until shortly before the run ends.
            def run():
                time.sleep(3)
                samples.append(android_gpu_sample(adb))
                time.sleep(max(1, args.seconds - 6))
                samples.append(android_gpu_sample(adb))
            threading.Thread(target=run, daemon=True).start()
        begin = time.monotonic()
        rc, text = stream(adb + ["shell", shell], args.seconds + 240, sampler)
        gpu = {}
        good = [x for x in samples if x]
        if len(good) == 2 and good[1][0] > good[0][0]:
            gpu["gpu.uid2000.busy_pct"] = f"{100.0 * (good[1][1] - good[0][1]) / (good[1][0] - good[0][0]):.2f}"
        else:
            gpu["gpu.uid2000.busy_pct"] = "unavailable"
        results.append(record_cell(label, opts, rc, text, time.monotonic() - begin, gpu))
    return results

def record_cell(label, opts, rc, text, seconds, gpu):
    summary = parse_summary(text)
    summary.update(gpu)
    record = {"cell": label, "options": opts, "exit": rc, "seconds": round(seconds, 1), "summary": summary}
    if rc != 0:
        record["output"] = text[-4000:]
    skipped = text.startswith("SKIP")
    state = "SKIP" if skipped else ("PASS" if rc == 0 else "FAIL")
    print(state, label, summary.get("config", ""), f"{record['seconds']}s", flush=True)
    for key in ("consumer.video.arrival.ms", "consumer.video.render_jitter.p1_p99.ms", "consumer.av_offset.abs.ms",
                "consumer.video.deadline_misses", "consumer.cpu.percent", "producer.cpu.percent", "source.cpu.percent",
                "consumer.result", "producer.result"):
        if key in summary:
            print(f"  {key}={summary[key]}", flush=True)
    for key in sorted(gpu):
        print(f"  {key}={gpu[key]}", flush=True)
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
    cells = list(SETS[args.set]) + (ANDROID_EXTRA.get(args.set, []) if args.adb else [])
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
