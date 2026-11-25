#!/usr/bin/env python3
"""Drain handshake timing from a run_drain_logged.sh log.

For each cycle of the long-lived consumer (framelinkavsrc), times from "all
tracks EOS: drain report pending" to the drain outcome ("sent", "cancelled",
"not sent"), and the margin from "sent" to the first "stopping session" after
it: how much earlier the report was written than the teardown that would
cancel it. Times are the consumer process's GStreamer log clock. For the fresh
producers (framelinkavsink), counts reports received and not received.

usage: drain_margin.py LOG
"""
import re, statistics, sys

LINE = re.compile(r'^(\d+):(\d+):(\d+\.\d+)\s+(\d+)\s+\S+\s+\w+\s+(framelinkavsrc|framelinkavsink)\s.*?> (.*)$')
src, sink = {}, {'received': 0, 'not received': 0}
for raw in open(sys.argv[1], errors='replace'):
    m = LINE.match(raw.strip())
    if not m:
        continue
    t = int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3])
    pid, cat, msg = int(m[4]), m[5], m[6]
    if cat == 'framelinkavsink':
        for k in sink:
            if msg == f'drain report {k}':
                sink[k] += 1
        continue
    src.setdefault(pid, []).append((t, msg))

pid = max(src, key=lambda p: len(src[p]))  # the long-lived consumer
cycles, cur = [], None
for t, msg in src[pid]:
    if msg.startswith('all tracks EOS'):
        cur = {'eos': t}
        cycles.append(cur)
    elif cur is not None and msg.startswith('drain report'):
        cur['outcome'], cur['at'] = msg, t
    elif cur is not None and msg == 'stopping session' and 'stop' not in cur:
        cur['stop'] = t

def ms(x):
    return f'{x * 1e3:.2f}'

outcomes = {}
for c in cycles:
    outcomes[c.get('outcome', 'no outcome')] = outcomes.get(c.get('outcome', 'no outcome'), 0) + 1
margins = [c['stop'] - c['at'] for c in cycles if c.get('outcome') == 'drain report sent' and 'stop' in c]
lag = [c['at'] - c['eos'] for c in cycles if 'at' in c]
print(f'long-lived consumer pid {pid}: {len(cycles)} cycles; outcomes {outcomes}')
print(f'fresh producers: drain report received {sink["received"]}, not received {sink["not received"]}')
if lag:
    print(f'EOS to outcome (ms): min {ms(min(lag))}, median {ms(statistics.median(lag))}, max {ms(max(lag))}')
if margins:
    q = sorted(margins)
    print(f'report-to-teardown margin (ms): min {ms(q[0])}, median {ms(statistics.median(q))}, max {ms(q[-1])}; '
          f'under 1 ms: {sum(x < 1e-3 for x in q)}, under 0.5 ms: {sum(x < 5e-4 for x in q)}, negative: {sum(x < 0 for x in q)}')
for i, c in enumerate(cycles):
    if c.get('outcome') != 'drain report sent':
        print(f'cycle {i}: {c.get("outcome", "no outcome")}' + (f', stop {ms(c["stop"] - c["eos"])} ms after EOS' if 'stop' in c else ''))
