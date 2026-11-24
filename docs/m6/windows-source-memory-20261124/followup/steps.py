#!/usr/bin/env python3
"""Step shape of a source's private memory (resources.csv from the native bench).

For each file: the +36 KB steps (an increase of exactly 36 KB between
consecutive samples, one heap commit) after the first 30 s, their first and
last time, the median interval, how long memory stayed flat after the last
step, and the net of all other changes after 30 s.

usage: steps.py RESOURCES_CSV [...]
"""
import csv, statistics, sys

for path in sys.argv[1:]:
    rows = [(int(r['t_ns']), int(r['memory_kb'])) for r in csv.DictReader(open(path))]
    t0, end = rows[0][0], rows[-1][0]
    steps, other = [], 0
    for (ta, ma), (tb, mb) in zip(rows, rows[1:]):
        if (tb - t0) / 1e9 < 30:
            continue
        if mb - ma == 36:
            steps.append((tb - t0) / 1e9)
        else:
            other += mb - ma
    span = (end - t0) / 1e9
    if steps:
        gaps = [b - a for a, b in zip(steps, steps[1:])]
        med = statistics.median(gaps) if gaps else float('nan')
        print(f'{path}: {span:.0f} s, {len(steps)} steps of 36 KB, first/last {steps[0]:.0f}/{steps[-1]:.0f} s, '
              f'median interval {med:.1f} s, flat for {span - steps[-1]:.0f} s after the last; other net change {other:+d} KB')
    else:
        print(f'{path}: {span:.0f} s, no 36 KB steps after 30 s; other net change {other:+d} KB')
