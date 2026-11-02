#!/usr/bin/env python3
"""Fault-only latest-frame loss accounting; never a nominal no-drop FPS gate."""
import argparse,csv,json,pathlib,re,collections
p=argparse.ArgumentParser();p.add_argument('prefix');p.add_argument('--frames',type=int,required=True);p.add_argument('--fps',type=int,default=60);p.add_argument('--private-readback-bytes',type=int,required=True);p.add_argument('--core-traces',nargs='+',required=True);p.add_argument('--client-trace',required=True);a=p.parse_args()
def rows(path):
 with open(path) as f:return list(csv.DictReader(f))
c=rows(a.prefix+'.consumer.csv');producer=rows(a.prefix+'.producer.csv');ids=[int(r['identity']) for r in c]
assert [int(r['identity']) for r in producer]==list(range(a.frames)),'producer did not finish'
assert ids and ids==sorted(set(ids)) and ids[0]==0 and ids[-1]==a.frames-1,'consumer did not recover to final identity'
assert all(int(r['pts_ns'])==int(r['identity'])*10**9//a.fps and int(r['source_marker_ns'])<=int(r['arrival_ns'])<=int(r['retired_ns']) for r in c),'changed timeline or ordering'
sends={};receives={};gpu={};ipc=collections.defaultdict(list);private=0;roles=[]
for filename in a.core_traces+[a.client_trace]:
 lines=pathlib.Path(filename).read_text().splitlines();footer=[x for x in lines if x.startswith('# dropped_events=')]
 assert len(footer)==1 and re.match(r'# dropped_events=0,rejected_threads=0,',footer[0]),'trace loss'
 events=list(csv.DictReader(x for x in lines if not x.startswith('#')));counts=collections.Counter()
 for r in events:
  op=r['operation'];counts[op]+=1;k=(r['channel_id'],int(r['fence']),int(r['pts_ns']))
  if op=='ipc.frame_received':ipc[k[0]].append((int(r['start_ns']),k,int(r['result'])))
  if r['result']!='0':continue
  if op in ['flEndSubmit','flAcquireFrame','benchmark.gpu_complete']:
   target=sends if op=='flEndSubmit' else receives if op=='flAcquireFrame' else gpu
   assert k not in target,'ambiguous identity';target[k]=int(r['start_ns'])+(int(r['duration_ns']) if op=='flAcquireFrame' else 0)
  if op=='benchmark.private_full_readback':assert int(r['payload_bytes'])==a.private_readback_bytes;private+=1
 if filename!=a.client_trace:
  if counts['flEndSubmit'] and counts['flAcquireFrame']:
   assert not counts['flImageMapPlanes'],'relay mapped payload';roles.append('relay')
  elif counts['flEndSubmit']:
   assert counts['flEndSubmit']==a.frames and counts['flImageMapPlanes']==a.frames,'producer trace incomplete'
   roles.append('producer')
  else:assert not counts['flImageMapPlanes'],'consumer mapped input';roles.append('consumer')
assert sorted(roles)==['consumer','producer','relay'],'missing process'
assert receives.keys()<=sends.keys(),'unsubmitted acquisition'
assert all(receives[k]>=sends[k] for k in receives),'incompatible clock/order'
missing=sends.keys()-receives.keys();replaced=set()
for channel,events in ipc.items():
 events.sort()
 for old,new in zip(events,events[1:]):
  if old[1] in missing and new[2]==1:replaced.add(old[1])
assert missing==replaced,'unexplained boundary loss'
assert {k[2] for k in missing}=={n*10**9//a.fps for n in set(range(a.frames))-set(ids)},'consumer loss not explained by boundary replacement'
assert len(gpu)==len(c)==private and all(k in receives and t>=receives[k] for k,t in gpu.items()),'incomplete GPU verification'
assert {k[2] for k in gpu}=={n*10**9//a.fps for n in ids},'GPU/CSV mismatch'
valid={n*10**9//a.fps for n in range(a.frames)}
assert all(k[2] in valid for k in sends),'changed boundary timestamps'
print(json.dumps({'fault_only':True,'nominal_performance_pass':False,'producer_identities':a.frames,'consumer_identities':len(c),'missing_consumer_identities':sorted(set(range(a.frames))-set(ids)),'boundary_joins':len(receives),'explained_mailbox_replacements':len(replaced),'gpu_completions':len(gpu),'trace_loss':0,'max_consumer_hold_ms':max((int(r['retired_ns'])-int(r['arrival_ns']))/1e6 for r in c),'max_producer_wait_ms':max(int(r['acquire_wait_ns'])/1e6 for r in producer)},indent=2))
