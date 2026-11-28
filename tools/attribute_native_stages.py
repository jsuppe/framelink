#!/usr/bin/env python3
"""Split a native relay capture's per-frame time by stage (decision 12, A2).

Reads the per-role video CSVs of one capture directory (source, relay,
receiver) and, when present, the source's pacing record (source.pace.csv,
written with FL_BENCH_PACE_DIAG=1). For each frame, by identity:

  source:   scheduled -> pace exit (late wake-up) -> acquire -> upload -> submit
  relay:    arrival -> forward
  receiver: arrival (pickup after the relay's forward) -> completed

Prints, and with --json writes, per-stage p50/p99/max and the frames where a
stage is far above its own p99 (p90 under 1000 frames), so a failure names the stage that stalled.
The pacing record splits a late wake-up into the sleeps (actual against
requested), yields, the thread's own CPU across the wait, and the previous
frame's loop tail (time mapping and CSV row).

  attribute_native_stages.py CAPTURE_DIR [--json OUT]
"""
import argparse, csv, json, os, sys


def load(path, key='identity'):
    """Rows by identity; rows cut short by an abort are skipped."""
    rows = {}
    if not os.path.exists(path):
        return rows
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            try:
                rows[int(r[key])] = {k: int(v) for k, v in r.items() if k and v not in (None, '')}
            except (TypeError, ValueError):
                continue
    return rows


def ms(a, b):
    return (b - a) / 1e6


SOURCE = {
    'source.late_wake': lambda s, r, c: ms(s['scheduled_ns'], s['acquire_begin_ns']),
    'source.acquire': lambda s, r, c: ms(s['acquire_begin_ns'], s['acquired_ns']),
    'source.upload': lambda s, r, c: ms(s['upload_begin_ns'], s['uploaded_ns']),
    'source.submit': lambda s, r, c: ms(s['submit_begin_ns'], s['submitted_ns']),
    'source_to_relay': lambda s, r, c: ms(s['submitted_ns'], r['arrival_ns']),
    'relay.forward': lambda s, r, c: ms(r['forward_begin_ns'], r['forwarded_ns']),
    'relay_to_receiver_pickup': lambda s, r, c: ms(r['forwarded_ns'], c['arrival_ns']),
    'receiver.complete': lambda s, r, c: ms(c['arrival_ns'], c['completed_ns']),
    'scheduled_to_receiver': lambda s, r, c: ms(s['scheduled_ns'], c['arrival_ns']),
}


def pace_stages(cpu_unit):
    out = {
        'pace.late': lambda p: ms(p['due_ns'], p['pace_exit_ns']),
        'pace.oversleep': lambda p: ms(p['max_sleep_request_ns'], p['max_sleep_ns']) if p['sleeps'] else 0.0,
        'pace.prev_tail': lambda p: ms(p['prev_tail_begin_ns'], p['prev_tail_end_ns']) if p['prev_tail_end_ns'] else 0.0,
    }
    if cpu_unit == 'ns':
        out['pace.off_cpu'] = lambda p: ms(p['pace_enter_ns'], p['pace_exit_ns']) - (p['cpu_exit'] - p['cpu_enter']) / 1e6
    return out


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else None


def analyze(d):
    src, rel, rcv = (load(os.path.join(d, n + '.video.csv')) for n in ('source', 'relay', 'receiver'))
    values = {k: {} for k in SOURCE}
    for i in sorted(set(src) | set(rel) | set(rcv)):
        for k, fn in SOURCE.items():
            try:
                values[k][i] = fn(src.get(i), rel.get(i), rcv.get(i))
            except (KeyError, TypeError):
                pass
    pace = load(os.path.join(d, 'source.pace.csv'))
    meta = {}
    if pace:
        try:
            meta = json.load(open(os.path.join(d, 'source.pace.json')))
        except (OSError, ValueError):
            meta = {}
        for k, fn in pace_stages(meta.get('cpu_unit')).items():
            values[k] = {}
            for i, p in pace.items():
                try:
                    values[k][i] = fn(p)
                except KeyError:
                    pass
    stages, outliers = {}, []
    for k, v in values.items():
        xs = list(v.values())
        if not xs:
            continue
        p99 = pct(xs, .99)
        worst = max(v, key=v.get)
        stages[k] = {'frames': len(xs), 'p50_ms': round(pct(xs, .5), 3), 'p99_ms': round(p99, 3), 'max_ms': round(v[worst], 3), 'max_frame': worst}
        # Outlier limit from the stage's own p99 (p90 under 1000 frames, where
        # the p99 would be the stall itself).
        base = p99 if len(xs) >= 1000 else pct(xs, .9)
        limit = max(3 * base, base + 3)
        for i, x in v.items():
            if x > limit:
                outliers.append({'frame': i, 'stage': k, 'ms': round(x, 3), 'limit_ms': round(limit, 3)})
    outliers.sort(key=lambda o: -o['ms'])
    detail = {}
    for o in outliers[:20]:
        i = o['frame']
        row = {k: round(v[i], 3) for k, v in values.items() if i in v}
        if i in pace:
            p = pace[i]
            row['pace.sleeps'], row['pace.yields'] = p.get('sleeps'), p.get('yields')
            row['pace.cpu_delta'] = p['cpu_exit'] - p['cpu_enter']
            row['pace.timer_resolution_100ns'] = p.get('timer_resolution_100ns')
        detail[str(i)] = row
    last = {n: (max(t) if t else None) for n, t in (('source', src), ('relay', rel), ('receiver', rcv))}
    return {'capture': os.path.basename(os.path.normpath(d)), 'rows': {'source': len(src), 'relay': len(rel), 'receiver': len(rcv), 'pace': len(pace)},
            'last_identity': last, 'pace_config': meta or None, 'stages': stages, 'outliers': outliers[:200], 'outlier_frames': detail}


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('capture')
    a.add_argument('--json')
    args = a.parse_args()
    r = analyze(args.capture)
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(r, f, indent=1)
    print('rows', r['rows'], 'last', r['last_identity'])
    for k, s in r['stages'].items():
        print(f"{k:26} p50 {s['p50_ms']:8.3f}  p99 {s['p99_ms']:8.3f}  max {s['max_ms']:9.3f} ms (frame {s['max_frame']})")
    for o in r['outliers'][:10]:
        print(f"outlier frame {o['frame']} {o['stage']} {o['ms']} ms (limit {o['limit_ms']})")
    return 0


if __name__ == '__main__':
    sys.exit(main())
