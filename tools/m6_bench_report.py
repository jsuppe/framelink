#!/usr/bin/env python3
"""Summarize M6 benchmark results and check them against the numeric budgets.

Input: JSON files written by tools/m6_bench_matrix.py. Repeats of one cell
(labels <cell>-r<N>) are aggregated: medians for typical values, the worst
round for tails and growth. With --budgets, every cell is checked against
docs/m6/budgets.json for its platform and the exit code reports violations.

  tools/m6_bench_report.py linux.json windows.json android.json --markdown
  tools/m6_bench_report.py soak.json --budgets docs/m6/budgets.json --platform linux
"""
import argparse, json, math, os, re, statistics, sys
from collections import OrderedDict

def dist(text):
    """'n=.. p50=.. p95=..' -> dict of floats (nan when absent)."""
    out = {}
    for k, v in re.findall(r"(\w+)=(-?[\d.]+|nan)", text or ""):
        out[k] = float(v)
    return out

def num(text, default=math.nan):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default

def spread(text):
    """'first..last min=a max=b' -> b - a (None when absent)."""
    d = dist(text)
    return int(d["max"] - d["min"]) if "min" in d and "max" in d else None

def delta(x, key):
    return num(x.get(f"gpu.device.active.{key}")) - num(x.get(f"gpu.device.idle.{key}"))

def gpu_busy(x):
    if "gpu.uid2000.busy_pct" in x:
        return num(x["gpu.uid2000.busy_pct"])
    if "gpu.producer.busiest_engine_pct" in x:
        return max(num(x.get("gpu.producer.busiest_engine_pct")), num(x.get("gpu.consumer.busiest_engine_pct")))
    return max(0.0, delta(x, "sm_pct")) if "gpu.device.active.sm_pct" in x else math.nan

def max_fn(rows, fn):
    vals = [v for v in (fn(x) for x in rows) if not math.isnan(v)]
    return max(vals) if vals else math.nan

def med_fn(rows, fn):
    vals = [v for v in (fn(x) for x in rows) if not math.isnan(v)]
    return statistics.median(vals) if vals else math.nan

def cell_name(label):
    return re.sub(r"-r\d+$", "", label)

def aggregate(doc):
    cells = OrderedDict()
    for rec in doc["cells"]:
        cells.setdefault(cell_name(rec["cell"]), []).append(rec)
    rows = OrderedDict()
    for name, recs in cells.items():
        ok = [r for r in recs if r["exit"] == 0 and r["summary"].get("complete") == "yes"]
        row = {"rounds": len(recs), "complete": len(ok), "config": recs[0]["summary"].get("config", ""),
               "errors": sorted({r["summary"].get("consumer.result", "") for r in recs if r not in ok} |
                                {r["summary"].get("producer.result", "") for r in recs if r not in ok} - {"EOS", ""})}
        if not ok:
            rows[name] = row
            continue
        s = [r["summary"] for r in ok]
        if any("cycles" in x for x in s):
            # Lifecycle (M6.3): per-cycle completion and long-lived growth per cycle.
            row.update({"lifecycle": True, "cycles_ok": min(x.get("cycles.ok", "0/0") for x in s),
                        "mem_kb_per_cycle": max(num(x.get("long.mem.slope.kb_per_cycle")) for x in s),
                        "handles_per_cycle": max(num(x.get("long.handles.slope.per_cycle")) for x in s),
                        "threads_per_cycle": max(num(x.get("long.threads.slope.per_cycle")) for x in s),
                        "wall_s": max(num(x.get("wall.s")) for x in s)})
            rows[name] = row
            continue
        if any("source.cpu.percent" in x for x in s):
            # Source-only reference: the test sources alone, no FrameLink.
            row.update({"source_cpu_pct": statistics.median(num(x.get("source.cpu.percent")) for x in s),
                        "source_cpu_pct_max": max(num(x.get("source.cpu.percent")) for x in s), "reference": True})
            rows[name] = row
            continue
        def worst(key, field, fn=max):
            vals = [dist(x.get(key)).get(field, math.nan) for x in s]
            vals = [v for v in vals if not math.isnan(v)]
            return fn(vals) if vals else math.nan
        def med(key, field=None):
            vals = [dist(x.get(key)).get(field, math.nan) if field else num(x.get(key)) for x in s]
            vals = [v for v in vals if not math.isnan(v)]
            return statistics.median(vals) if vals else math.nan
        misses = 0
        for x in s:
            for t in ("video", "audio"):
                m = re.match(r"(\d+)/", x.get(f"consumer.{t}.deadline_misses", "0/0"))
                misses += int(m.group(1)) if m else 0
        sides = [(x, side) for x in s for side in ("producer", "consumer")]
        def worst_side(key):
            vals = [num(x.get(f"{side}.{key}")) for x, side in sides]
            vals = [v for v in vals if not math.isnan(v)]
            return max(vals) if vals else math.nan
        def worst_spread(key):
            vals = [spread(x.get(f"{side}.{key}")) for x, side in sides]
            vals = [v for v in vals if v is not None]
            return max(vals) if vals else None
        row.update({
            "video_arrival_p50_ms": med("consumer.video.arrival.ms", "p50"),
            "video_arrival_p99_ms": worst("consumer.video.arrival.ms", "p99"),
            "video_arrival_max_ms": worst("consumer.video.arrival.ms", "max"),
            "audio_arrival_p99_ms": worst("consumer.audio.arrival.ms", "p99"),
            "audio_arrival_max_ms": worst("consumer.audio.arrival.ms", "max"),
            "video_render_jitter_ms": max(num(x.get("consumer.video.render_jitter.p1_p99.ms")) for x in s),
            "audio_render_jitter_ms": max(num(x.get("consumer.audio.render_jitter.p1_p99.ms")) for x in s),
            "video_interval_max_ms": worst("consumer.video.render_interval.ms", "max"),
            "av_offset_abs_max_ms": worst("consumer.av_offset.abs.ms", "max"),
            "deadline_misses": misses,
            "consumer_cpu_pct": med("consumer.cpu.percent"),
            "producer_cpu_pct": med("producer.cpu.percent"),
            "mem_slope_kb_per_min": worst_side("mem.slope.kb_per_min"),
            "handle_slope_per_min": worst_side("handles.slope.per_min"),
            "thread_slope_per_min": worst_side("threads.slope.per_min"),
            "handle_spread": worst_spread("handles"),
            "thread_spread": worst_spread("threads"),
            "seconds": min(num(x.get("seconds", "nan")) for x in s),
            "memory": s[0].get("consumer.memory", ""),
            # GPU: one comparable busy figure per platform (Windows: busiest
            # engine of either process; Android: GPU active time of the
            # benchmark uid; Linux: device-wide SM load above idle), plus the
            # device-wide NVIDIA deltas where available.
            "gpu_busy_pct": med_fn(s, gpu_busy),
            # Worst rounds, used by the budget rule (the Linux CPU path is bimodal).
            "consumer_cpu_pct_max": max(num(x.get("consumer.cpu.percent")) for x in s),
            "producer_cpu_pct_max": max(num(x.get("producer.cpu.percent")) for x in s),
            "gpu_busy_pct_max": max_fn(s, gpu_busy),
            "gpu_power_delta_w": med_fn(s, lambda x: delta(x, "power_w")),
            "gpu_pcie_mb_s": med_fn(s, lambda x: delta(x, "pcie_rx_mb_s") + delta(x, "pcie_tx_mb_s")),
            "gpu_fb_delta_mb": med_fn(s, lambda x: delta(x, "fb_mb")),
            "gpu_core_mhz": med_fn(s, lambda x: num(x.get("gpu.device.active.core_mhz"))),
            "gpu_process_dedicated_mb": med_fn(s, lambda x: max(num(x.get("gpu.producer.dedicated_mb"), math.nan), num(x.get("gpu.consumer.dedicated_mb"), math.nan))),
        })
        rows[name] = row
    return rows

COLUMNS = [("config", "Config", None), ("complete", "OK", None),
           ("video_arrival_p50_ms", "V arr p50", 2), ("video_arrival_p99_ms", "V arr p99", 2),
           ("video_arrival_max_ms", "V arr max", 2), ("audio_arrival_p99_ms", "A arr p99", 2),
           ("video_render_jitter_ms", "V jit", 2), ("audio_render_jitter_ms", "A jit", 2),
           ("av_offset_abs_max_ms", "|A/V| max", 2), ("deadline_misses", "Late", None),
           ("consumer_cpu_pct", "C CPU%", 1), ("producer_cpu_pct", "P CPU%", 1),
           ("mem_slope_kb_per_min", "Mem KB/min", 0), ("handle_spread", "dH", None), ("thread_spread", "dT", None),
           ("gpu_busy_pct", "GPU busy%", 1), ("gpu_power_delta_w", "GPU +W", 1), ("gpu_pcie_mb_s", "PCIe MB/s", 0),
           ("gpu_fb_delta_mb", "GPU +MB", 0)]

def fmt(v, digits):
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return "-"
    return f"{v:.{digits}f}" if digits is not None and isinstance(v, float) else str(v)

def markdown(platform, rows):
    out = [f"### {platform}", "", "| Cell | " + " | ".join(c[1] for c in COLUMNS) + " |",
           "|---|" + "---|" * len(COLUMNS)]
    for name, row in rows.items():
        if row.get("lifecycle"):
            out.append(f"| {name} | {row['config']} | {row['complete']}/{row['rounds']} | cycles ok {row['cycles_ok']}; long-lived growth per cycle: "
                       f"{row['mem_kb_per_cycle']:.2f} KB, {row['handles_per_cycle']:.3f} handles, {row['threads_per_cycle']:.3f} threads; {row['wall_s']:.0f} s |" + " |" * (len(COLUMNS) - 3))
            continue
        if row.get("reference"):
            out.append(f"| {name} | {row['config']} | {row['complete']}/{row['rounds']} | source alone: {fmt(row['source_cpu_pct'], 1)}% CPU |" + " |" * (len(COLUMNS) - 3))
            continue
        if row["complete"] == 0:
            out.append(f"| {name} | {row['config']} | 0/{row['rounds']} | " + " | ".join(["-"] * (len(COLUMNS) - 2)) +
                       f" |\n|  | failed: {'; '.join(row['errors'])[:160]} |")
            continue
        out.append(f"| {name} | " + " | ".join(
            (f"{row['complete']}/{row['rounds']}" if k == "complete" else fmt(row.get(k), d)) for k, _, d in COLUMNS) + " |")
    return "\n".join(out) + "\n"

# M6.3 lifecycle limits per cycle of the long-lived process, after warm-up:
# under 1.6 MB, 10 handles and 2 threads of growth over 100 cycles.
LIFECYCLE = {"mem_kb_per_cycle": 16.0, "handles_per_cycle": 0.1, "threads_per_cycle": 0.02}

def check(platform, rows, budgets):
    """Budget violations for one platform: list of strings."""
    b = budgets["platforms"].get(platform)
    if b is None:
        return [f"no budgets for platform {platform}"]
    problems = []
    for name, row in rows.items():
        if row.get("lifecycle") or name.startswith("lifecycle-"):
            if row["complete"] < row["rounds"] or not row.get("lifecycle"):
                problems.append(f"{name}: incomplete {row.get('errors')}")
                continue
            for key, limit in LIFECYCLE.items():
                if not row[key] <= limit:
                    problems.append(f"{name}: {key}={row[key]:.3f} exceeds {limit}")
            continue
        base = name[len("soak-"):] if name.startswith("soak-") else name
        limits = dict(b.get("cells", {}).get(base, {}))
        if not limits:
            problems.append(f"{name}: no budget for cell {base}")
            continue
        if limits.get("unsupported") or limits.get("reference"):
            continue
        if row["complete"] < row["rounds"]:
            problems.append(f"{name}: {row['rounds'] - row['complete']} of {row['rounds']} rounds incomplete {row['errors']}")
            continue
        for key, limit in limits.items():
            if key in ("unsupported", "note", "reference"):
                continue
            if key in GROWTH and not row.get("seconds", 0) >= GROWTH_MIN_S:
                continue
            value = row.get(key)
            if value is None or (isinstance(value, float) and math.isnan(value)) or value > limit:
                problems.append(f"{name}: {key}={fmt(value, 2)} exceeds {limit}")
    return problems

# Budget rule (M6.1). Applied to the baseline worst round / median per cell,
# so the budgets are reproducible from the committed baseline evidence.
SESSION_LATENCY_MS = 150.0
RULES = {
    # metric: (scale, floor added to the baseline, absolute cap)
    "video_arrival_p99_ms": (2.0, 5.0, SESSION_LATENCY_MS / 2),
    "video_arrival_max_ms": (2.0, 10.0, SESSION_LATENCY_MS / 2),
    "audio_arrival_p99_ms": (2.0, 5.0, SESSION_LATENCY_MS / 2),
    "audio_arrival_max_ms": (2.0, 10.0, SESSION_LATENCY_MS / 2),
    "video_render_jitter_ms": (2.0, 1.0, None),
    "audio_render_jitter_ms": (2.0, 1.0, None),
    "av_offset_abs_max_ms": (2.0, 1.0, 5.0),
    "consumer_cpu_pct_max": (1.5, 2.0, None),
    "producer_cpu_pct_max": (1.5, 2.0, None),
    "gpu_busy_pct_max": (1.5, 2.0, None),
    "handle_spread": (2.0, 8, None),
    "thread_spread": (2.0, 4, None),
}
FIXED = {"deadline_misses": 0}
# Growth limits per process, checked only on runs of at least GROWTH_MIN_S
# (a one-minute run cannot separate warm-up from growth).
GROWTH = {"mem_slope_kb_per_min": 64, "handle_slope_per_min": 1.0, "thread_slope_per_min": 0.5}
GROWTH_MIN_S = 600

def derive(platform_rows, unsupported):
    """Budgets per cell from one platform's aggregated baseline rows."""
    cells = {}
    for name, row in platform_rows.items():
        if row.get("reference"):
            cells[name] = {"reference": True, "note": "test sources alone; not budgeted"}
            continue
        if name in unsupported:
            cells[name] = {"unsupported": True, "note": unsupported[name]}
            continue
        if row["complete"] < row["rounds"]:
            raise SystemExit(f"baseline cell {name} incomplete ({row['complete']}/{row['rounds']}); classify it unsupported or rerun")
        limits = {}
        for key, (scale, floor, cap) in RULES.items():
            base = row.get(key)
            if base is None or (isinstance(base, float) and math.isnan(base)):
                continue
            value = max(scale * base, base + floor)
            limits[key] = round(min(value, cap) if cap is not None else value, 2)
            if cap is not None and base > cap:
                raise SystemExit(f"baseline {name} {key}={base} already exceeds the cap {cap}")
        limits.update(FIXED)
        limits.update(GROWTH)
        if "traced" in name:
            # The recorder fills its preallocated capacity; growth is bounded by it.
            limits.pop("mem_slope_kb_per_min")
        cells[name] = limits
    return cells

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("results", nargs="+")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--budgets")
    ap.add_argument("--platform", help="budget platform key (default: the results' platform label prefix)")
    ap.add_argument("--derive", help="write budgets derived from these baseline results to this file")
    ap.add_argument("--unsupported", action="append", default=[], metavar="PLATFORM:CELL=REASON",
                    help="cells excluded from budgets (with --derive)")
    a = ap.parse_args()
    if a.derive:
        out = {"rule": {"scaled": {k: list(v) for k, v in RULES.items()}, "fixed": FIXED,
                        "growth": GROWTH, "growth_min_seconds": GROWTH_MIN_S,
                        "session_latency_ms": SESSION_LATENCY_MS,
                        "text": "limit = min(cap, max(scale*baseline, baseline+floor)); baseline is the worst "
                                "of the baseline rounds for every metric"},
               "platforms": {}}
        for path in a.results:
            doc = json.load(open(path))
            key = doc["platform"].split("-")[0].split(":")[0]
            unsupported = {}
            for u in a.unsupported:
                plat, rest = u.split(":", 1)
                cell, reason = rest.split("=", 1)
                if plat == key:
                    unsupported[cell] = reason
            entry = out["platforms"].setdefault(key, {"baselines": [], "cells": {}})
            entry["baselines"].append({"platform": doc["platform"], "set": doc.get("set"), "seconds": doc["seconds"],
                                       "repeat": doc.get("repeat", 1), "file": os.path.basename(path)})
            entry["cells"].update(derive(aggregate(doc), unsupported))
        with open(a.derive, "w") as f:
            json.dump(out, f, indent=1)
            f.write("\n")
        print("wrote", a.derive)
        return 0
    budgets = json.load(open(a.budgets)) if a.budgets else None
    failed = False
    for path in a.results:
        doc = json.load(open(path))
        rows = aggregate(doc)
        if a.markdown:
            print(markdown(doc["platform"], rows))
        else:
            print(json.dumps({"platform": doc["platform"], "cells": rows}, indent=1))
        if budgets:
            key = a.platform or doc["platform"].split("-")[0].split(":")[0]
            problems = check(key, rows, budgets)
            for p in problems:
                print("BUDGET", doc["platform"], p)
            print("budgets", doc["platform"], "pass" if not problems else f"{len(problems)} violations")
            failed |= bool(problems)
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
