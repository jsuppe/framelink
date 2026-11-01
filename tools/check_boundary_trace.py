#!/usr/bin/env python3
"""Strict per-identity accounting for a single-epoch native boundary benchmark."""
import argparse,collections,csv,gzip,json,pathlib,re
p=argparse.ArgumentParser();p.add_argument('files',nargs='+');p.add_argument('--frames',type=int,required=True);p.add_argument('--hops',type=int,choices=[1,2],required=True);p.add_argument('--fps',type=int,default=60);p.add_argument('--gpu',action='store_true');a=p.parse_args()
assert len(a.files)==a.hops+1,'missing process trace'
sends={};receives={};completed={};roles=[];readbacks=0;channels=collections.defaultdict(set)
for filename in a.files:
 opener=gzip.open if filename.endswith('.gz') else open
 with opener(filename,'rt') as f:lines=f.read().splitlines()
 foot=[x for x in lines if x.startswith('# dropped_events=')]
 assert len(foot)==1 and re.match(r'# dropped_events=0,rejected_threads=0,',foot[0]),(filename,'trace loss/footer')
 counts=collections.Counter();maps=0
 for r in csv.DictReader(x for x in lines if not x.startswith('#')):
  if r['operation']=='flImageMap':maps+=1
  if r['result']!='0':continue
  op=r['operation'];counts[op]+=1
  if op=='benchmark.private_verification_readback':
   assert a.gpu and int(r['payload_bytes'])==36,'unexpected verification copy'
   readbacks+=1
  if op not in ('flEndSubmit','flAcquireFrame','benchmark.gpu_complete'):continue
  k=(r['channel_id'],int(r['fence']),int(r['pts_ns']));target=sends if op=='flEndSubmit' else completed if op=='benchmark.gpu_complete' else receives
  assert k not in target,(filename,'ambiguous identity',k)
  target[k]=int(r['start_ns'])+(int(r['duration_ns']) if op=='flAcquireFrame' else 0)
  if op!='benchmark.gpu_complete':channels[k[0]].add(k[2])
 sent=counts['flEndSubmit'];got=counts['flAcquireFrame']
 if sent and got:role='relay';assert sent==got==a.frames and maps==0,'relay lost frames or mapped payload'
 elif sent:role='producer';assert sent==a.frames and maps==a.frames,'producer counts'
 else:role='consumer';assert got==a.frames and maps==(0 if a.gpu else a.frames),'consumer counts'
 roles.append(role)
if a.gpu:
 assert readbacks==a.frames,'missing verification readback accounting'
 assert len(completed)==a.frames and all(k in receives and t>=receives[k] for k,t in completed.items()),'missing/malformed GPU completion'
else:assert not completed,'unexpected GPU mode'
assert sorted(roles)==sorted(['producer','consumer']+(['relay'] if a.hops==2 else [])),roles
assert sends.keys()==receives.keys() and len(sends)==a.frames*a.hops,'unmatched/missing boundary identity'
assert len(channels)==a.hops and all(v=={n*10**9//a.fps for n in range(a.frames)} for v in channels.values()),'changed/missing timeline'
assert all(receives[k]>=sends[k] for k in sends),'incompatible clock/order'
print(json.dumps({'unique_boundary_joins':len(sends),'frames':a.frames,'hops':a.hops,'roles':roles,'relay_payload_maps':0,'trace_loss':0,'gpu_completions':len(completed),'private_verification_readback_bytes':readbacks*36,'physical_output_qualified':False},indent=2))
