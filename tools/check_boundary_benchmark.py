#!/usr/bin/env python3
"""Validate native boundary timing measurements; CSV alone is not GPU/PCM proof."""
import argparse,csv,gzip,json,math,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('prefix',nargs='+');p.add_argument('--frames',type=int,default=3720);p.add_argument('--warmup',type=int,default=120);p.add_argument('--fps',type=int,default=60);p.add_argument('--minimum-fps',type=float,default=59.4);a=p.parse_args()
assert 0<=a.warmup<a.frames-1 and a.fps>0

def rows(path):
 if not pathlib.Path(path).exists() and pathlib.Path(path+'.gz').exists():path+='.gz'
 opener=gzip.open if str(path).endswith('.gz') else open
 with opener(path,'rt') as f:return [{k:int(v) for k,v in row.items()} for row in csv.DictReader(f)]
def stats(values):
 v=sorted(values)
 return {k:v[max(0,math.ceil(q*len(v))-1)] for k,q in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
results=[]
for prefix in a.prefix:
 c=rows(prefix+'.consumer.csv');w=rows(prefix+'.producer.csv')
 assert len(c)==len(w)==a.frames,(prefix,'missing records',len(c),len(w))
 assert [r['identity'] for r in c]==list(range(a.frames)),(prefix,'consumer identity gap/repeat')
 assert [r['identity'] for r in w]==list(range(a.frames)),(prefix,'producer identity gap/repeat')
 assert all(r['pts_ns']==r['identity']*10**9//a.fps for r in c),(prefix,'changed timeline')
 assert all(r['source_marker_ns']<=r['arrival_ns']<=r['retired_ns'] for r in c),(prefix,'clock/order')
 assert all(x['arrival_ns']<y['arrival_ns'] for x,y in zip(c,c[1:])),(prefix,'nonmonotonic arrival')
 c=c[a.warmup:];w=w[a.warmup:]
 rate=(len(c)-1)*1e9/(c[-1]['arrival_ns']-c[0]['arrival_ns'])
 result={'prefix':prefix,'total_identities':a.frames,'measured_identities':len(c),'fresh_fps':rate,
 'source_marker_to_arrival_ms':stats([(r['arrival_ns']-r['source_marker_ns'])/1e6 for r in c]),
 'consumer_hold_ms':stats([(r['retired_ns']-r['arrival_ns'])/1e6 for r in c]),
 'producer_acquire_wait_ms':stats([r['acquire_wait_ns']/1e6 for r in w]),
 'delivery_interval_ms':stats([(y['arrival_ns']-x['arrival_ns'])/1e6 for x,y in zip(c,c[1:])]),
 'rate_pass':rate>=a.minimum_fps,'gpu_completion_verified_by_csv':False,'pcm_qualified':False}
 results.append(result)
report={'cases':results}
if len(results)==2:
 delta=results[1]['source_marker_to_arrival_ms']['p95']-results[0]['source_marker_to_arrival_ms']['p95']
 report['comparison']={'second_minus_first_p95_ms':delta,'budget_ms':1000/a.fps,'pass':delta<=1000/a.fps}
print(json.dumps(report,indent=2))
assert all(r['rate_pass'] for r in results),'rate gate failed'
assert report.get('comparison',{}).get('pass',True),'extra boundary p95 budget failed'
