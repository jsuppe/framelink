#!/usr/bin/env python3
"""Summarize one smaps_sampler.py run of a lifecycle cell.

- Gate view (the bench summary): cycles, the long-lived peer's memory slope
  over cycles 5-99 as the bench reports it, the record's "steps" (cycles more
  than 2 MB from the median of cycles 5-99) and the slope without them.
- Memory kind (samples.jsonl, after the first 30 s): resident memory split
  into anonymous mappings of 8 MiB or more (malloc arenas and large blocks)
  and everything else; range (max - min) and change of each.
- Large allocations (the shim's log, optional): count, sizes, threads, frees.

usage: analyze.py RUN_DIR [ALLOC_LOG_DIR]
"""
import collections, glob, json, os, statistics, sys

run = sys.argv[1]
summ = dict(l.rstrip('\n').split('=', 1) for l in open(glob.glob(os.path.join(run, 'bench', '*.summary.txt'))[0]) if '=' in l)
series = [c.split('|') for c in summ['long.series.result_frames_samples_memkb_handles_threads'].split(';') if c]
mem = [int(c[3]) for c in series]

def slope(points):
    xs, ys = zip(*points); mx, my = statistics.mean(xs), statistics.mean(ys)
    return sum((x - mx) * (y - my) for x, y in points) / sum((x - mx) ** 2 for x in xs)

window = list(enumerate(mem))[5:]
med = statistics.median(m for _, m in window)
steps = [(i, m) for i, m in window if abs(m - med) > 2048]
rest = [(i, m) for i, m in window if abs(m - med) <= 2048]
print(f"{os.path.basename(run)}: cycles.ok {summ['cycles.ok']}, complete {summ.get('complete')}, "
      f"bench memory slope {summ['long.mem.slope.kb_per_cycle']} KB/cycle (gate 16)")
print(f"  cycles 5-99: median {med / 1024:.1f} MB, steps {len(steps)}"
      + (f" (cycles {', '.join(str(i) for i, _ in steps)}; {min(m - med for _, m in steps) / 1024:+.0f} to {max(m - med for _, m in steps) / 1024:+.0f} MB)" if steps else '')
      + f", slope without steps {slope(rest):.2f} KB/cycle")

big, other, t = [], [], []
for line in open(os.path.join(run, 'samples.jsonl')):
    r = json.loads(line)
    if r['t'] < 30:
        continue
    b = sum(m.get('Anonymous', 0) for m in r['maps'] if m['perms'].endswith('p'))
    t.append(r['t']); big.append(b); other.append(r['rollup']['Rss'] - b)
if not t:
    print('  samples after 30 s: none (the sampler did not find the long-lived peer)')
else:
  print(f"  samples after 30 s: {len(t)}; large anonymous resident {min(big) / 1024:.1f}-{max(big) / 1024:.1f} MB "
        f"(range {(max(big) - min(big)) / 1024:.1f} MB); everything else {min(other) / 1024:.1f}-{max(other) / 1024:.1f} MB "
        f"(range {(max(other) - min(other)) / 1024:.1f} MB, first {other[0] / 1024:.1f}, last {other[-1] / 1024:.1f})")

if len(sys.argv) > 2:
    allocs, frees = collections.Counter(), 0
    for f in glob.glob(os.path.join(sys.argv[2], 'alloc.*')):
        for line in open(f):
            if line.startswith('ALLOC'):
                p = dict(kv.split('=', 1) for kv in line.split()[2:])
                allocs[(p['thread'], p['size'])] += 1
            elif line.startswith('FREE'):
                frees += 1
    print(f"  allocations of 8 MiB or more: {sum(allocs.values())} ({', '.join(f'{n} x {s} B by {th}' for (th, s), n in allocs.most_common())}); frees {frees}")
