#!/usr/bin/env python3
"""Summarize raw trace CSV. CPU spans and GPU durations are never added together."""
import argparse, csv, glob, math, statistics
from collections import defaultdict
p=argparse.ArgumentParser(__doc__)
p.add_argument('files',nargs='+',help='CSV files or glob patterns')
a=p.parse_args()
groups=defaultdict(list)
all_rows=[]
for pattern in a.files:
    for path in glob.glob(pattern):
        with open(path,encoding='utf-8') as f:
            lines=f.readlines()
        for line in lines:
            if line.startswith('#'):print(path, line.strip())
        for row in csv.DictReader(line for line in lines if not line.startswith('#')):
            groups[(path,row['operation'])].append(row)
            all_rows.append((path,row))
for (path,op),rows in sorted(groups.items()):
    d=sorted(int(r['duration_ns']) for r in rows)
    def percentile(p):return d[max(0,math.ceil(p*len(d))-1)]/1000
    results=defaultdict(int)
    for row in rows:results[row['result']]+=1
    print(f'{path} {op}: n={len(d)} us p50={percentile(.5):.3f} p95={percentile(.95):.3f} p99={percentile(.99):.3f} max={d[-1]/1000:.3f} results={dict(results)}')
    if op=='flAcquireFrame':
        channels=defaultdict(list)
        for row in rows:
            if row['result']=='0':channels[row['channel_id']].append(row)
        for channel,frames in channels.items():
            frames.sort(key=lambda r:int(r['start_ns']))
            times=[int(r['start_ns'])+int(r['duration_ns']) for r in frames]
            gaps=[(y-x)/1e6 for x,y in zip(times,times[1:])]
            if gaps:print(f'  channel={channel} delivery interval ms p50={statistics.median(gaps):.3f} max={max(gaps):.3f} (API delivery, NOT presentation)')

# Exact tuple joins only; refuse ambiguous reset/reconnect identities.
submits=defaultdict(list)
for path,row in all_rows:
    if row['operation']=='flEndSubmit' and row['result']=='0':
        submits[(row['channel_id'],row['fence'],row['pts_ns'])].append(row)
latencies=[];ambiguous=0
for path,row in all_rows:
    if row['operation']!='flAcquireFrame' or row['result']!='0':continue
    matches=submits.get((row['channel_id'],row['fence'],row['pts_ns']),[])
    if len(matches)!=1:
        if matches:ambiguous+=1
        continue
    delay=int(row['start_ns'])+int(row['duration_ns'])-int(matches[0]['start_ns'])
    if delay>=0:latencies.append(delay/1e6)
if latencies:
    print(f'Exact channel/fence/PTS joins: n={len(latencies)} submit-entry to acquire-return ms median={statistics.median(latencies):.3f} max={max(latencies):.3f}; ambiguous={ambiguous}')
    print('Only meaningful on one host/boot with a shared monotonic clock. Not display latency; GPU_SYNC delivery may precede GPU completion.')
