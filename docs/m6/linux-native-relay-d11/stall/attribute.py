#!/usr/bin/env python3
"""Attribute relay-cell stalls to a stage, from the per-role video CSVs.

For each frame (by identity) the path is:
  source: scheduled -> acquire_begin -> acquired -> upload_begin -> uploaded -> submit_begin -> submitted
  relay:  arrival -> forward_begin -> forwarded
  receiver: arrival -> completed
Prints per-stage percentiles and the frames where any stage is far above its p99,
plus the largest receiver acquisition gaps.
  attribute.py CAPTURE_DIR [period_ms]
"""
import csv, sys, os, statistics

d = sys.argv[1]; period = float(sys.argv[2]) if len(sys.argv) > 2 else 16.667

def load(role):
    p = os.path.join(d, role + '.video.csv')
    if not os.path.exists(p): return {}
    with open(p) as f: return {int(r['identity']): {k: int(v) for k, v in r.items() if v and v.lstrip('-').isdigit()} for r in csv.DictReader(f) if r.get('identity','').isdigit()}

src, rel, rcv = load('source'), load('relay'), load('receiver')
ids = sorted(set(src) | set(rel) | set(rcv))
ms = lambda a, b: (b - a) / 1e6
stages = {
    'src.acquire': lambda s, r, c: ms(s['acquire_begin_ns'], s['acquired_ns']),
    'src.upload': lambda s, r, c: ms(s['upload_begin_ns'], s['uploaded_ns']),
    'src.submit': lambda s, r, c: ms(s['submit_begin_ns'], s['submitted_ns']),
    'src.late_start': lambda s, r, c: ms(s['scheduled_ns'], s['acquire_begin_ns']),
    'src->relay': lambda s, r, c: ms(s['submitted_ns'], r['arrival_ns']),
    'relay.forward': lambda s, r, c: ms(r['forward_begin_ns'], r['forwarded_ns']),
    'relay->rcv': lambda s, r, c: ms(r['forwarded_ns'], c['arrival_ns']),
    'rcv.complete': lambda s, r, c: ms(c['arrival_ns'], c['completed_ns']),
    'total': lambda s, r, c: ms(s['scheduled_ns'], c['arrival_ns']),
}
vals = {k: {} for k in stages}
for i in ids:
    s, r, c = src.get(i), rel.get(i), rcv.get(i)
    for k, fn in stages.items():
        try:
            if k.startswith('src') and not s: continue
            v = fn(s, r, c)
            if v == v and abs(v) < 1e6: vals[k][i] = v
        except (KeyError, TypeError):
            pass

def pct(xs, q):
    xs = sorted(xs); return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else float('nan')

print(f"frames: source {len(src)} relay {len(rel)} receiver {len(rcv)}; last ids: source {max(src) if src else '-'} relay {max(rel) if rel else '-'} receiver {max(rcv) if rcv else '-'}")
for k, v in vals.items():
    xs = list(v.values())
    if xs: print(f"{k:15} p50 {pct(xs,.5):7.3f} p99 {pct(xs,.99):7.3f} max {max(xs):8.3f} ms (frame {max(v, key=v.get)})")
print("outliers (stage > max(3*p99, p99+3 ms)):")
for k, v in vals.items():
    xs = list(v.values())
    if not xs: continue
    lim = max(3 * pct(xs, .99), pct(xs, .99) + 3)
    for i, x in sorted(v.items()):
        if x > lim:
            row = ' '.join(f"{kk}={vals[kk].get(i, float('nan')):.2f}" for kk in stages)
            print(f"  frame {i} {k}={x:.2f} ms | {row}")
gaps = sorted(((ms(rcv[a]['arrival_ns'], rcv[b]['arrival_ns']), b) for a, b in zip(sorted(rcv), sorted(rcv)[1:])), reverse=True)[:5]
print("largest receiver arrival gaps:", ', '.join(f"{g:.2f} ms before frame {b}" for g, b in gaps))
