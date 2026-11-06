#!/usr/bin/env python3
"""Join benchmark video timing records only after verifying constant PTS mapping.

This diagnoses producer versus post-push lateness, not allocation identity,
GPU completion, acoustic sync or physical presentation. Expected frame count
is mandatory: equal but truncated files must not pass the join gate.
"""
import argparse
import csv
import json
import math


def read(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def distribution(values):
    ordered = sorted(values)
    return {name: ordered[max(0, math.ceil(len(ordered) * p) - 1)] / 1e6
            for name, p in (("p50", .5), ("p95", .95), ("p99", .99), ("max", 1))}


def analyze(producer, consumer, expected, budget_ns):
    if expected <= 0 or len(producer) != expected or len(consumer) != expected:
        raise ValueError("raw frame counts do not match the independent expected count")
    pp = [int(r["pts_ns"]) for r in producer]
    cp = [int(r["pts_ns"]) for r in consumer]
    if any(b <= a for pts in (pp, cp) for a, b in zip(pts, pts[1:])):
        raise ValueError("duplicate or non-monotonic video PTS")
    offset = cp[0] - pp[0]
    if any(c - p != offset for p, c in zip(pp, cp)):
        raise ValueError("video PTS mapping is not constant; positional join rejected")
    push = [int(r["push_lateness_ns"]) for r in producer]
    arrival = [int(r["arrival_lateness_ns"]) for r in consumer]
    residual = [a - p for a, p in zip(arrival, push)]
    late = [{"index": i, "producer_pts_ns": pp[i], "consumer_pts_ns": cp[i],
             "push_lateness_ns": push[i], "arrival_lateness_ns": arrival[i],
             "post_push_residual_ns": residual[i]}
            for i in range(expected) if arrival[i] > budget_ns]
    return {"join": "inferred_constant_pts_mapping", "frames": expected,
            "pts_offset_ns": offset, "arrival_budget_ns": budget_ns,
            "arrival_misses": len(late), "late_frames": late,
            "push_ms": distribution(push), "arrival_ms": distribution(arrival),
            "post_push_residual_ms": distribution(residual),
            "scope": "All frames including startup/drain. Offset inferred from first records: "
                     "cannot independently exclude a whole-stream constant shift; not identity proof. "
                     "Residual includes producer copy, transport, consumer work and clock-mapping "
                     "error; not a root-cause proof."}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("producer")
    p.add_argument("consumer")
    p.add_argument("--expected", type=int, required=True)
    p.add_argument("--budget-ms", type=float, required=True)
    a = p.parse_args()
    if not math.isfinite(a.budget_ms) or a.budget_ms < 0:
        p.error("budget must be finite and non-negative")
    try:
        result = analyze(read(a.producer), read(a.consumer), a.expected,
                         round(a.budget_ms * 1e6))
    except (ValueError, KeyError, OSError) as e:
        p.exit(2, str(e) + "\n")
    print(json.dumps(result, indent=2))
    # Successful joining is not successful latency qualification.
    return 1 if result["arrival_misses"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
