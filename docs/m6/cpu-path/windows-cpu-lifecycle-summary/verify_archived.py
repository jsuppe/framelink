#!/usr/bin/env python3
"""Offline count/PCM/resource reconstruction; no runtime or acceptance-budget changes."""
from pathlib import Path
import json,math,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent.parent
reports=[];fresh_audio=0
for p in sorted(root.glob('windows-cpu-lifecycle-*/persistent-*/data/status.json')):
 status=json.loads(p.read_text());assert status['state']=='finished';case=status['cases'][-1];assert case['label'].endswith('-100') and case['exit']==0
 folder=p.parent/case['label'];frames=150 if '-4k-' in case['label'] else 300
 def fields(f):return dict(x.split('=',1) for x in f.read_text().splitlines() if '=' in x)
 long=fields(folder/(case['label']+'-long.txt'));rows=[];restart_producer='restart-producer' in case['label']
 for i in range(100):
  a=long['cycle.'+str(i)].split('|');assert a[:3]==['EOS',str(frames),'240000'];rows.append([i]+[float(x) for x in a[3:]])
  f=fields(folder/(case['label']+'-fresh-'+str(i)+'.txt'));assert f['result']=='EOS'
  assert f['pushed.frames' if restart_producer else 'frames']==str(frames)
  assert f['pushed.samples' if restart_producer else 'samples']=='240000'
  if not restart_producer:
   assert f['audio.contiguous']=='yes' and f['queue.accounting']=='complete';fresh_audio+=1
 data=rows[5:];meanx=sum(x[0] for x in data)/95;den=sum((x[0]-meanx)**2 for x in data);slopes={}
 for j,key,limit in [(1,'memory_kib_per_cycle',16),(2,'handles_per_cycle',.1),(3,'threads_per_cycle',.02)]:
  meany=sum(x[j] for x in data)/95;value=sum((x[0]-meanx)*(x[j]-meany) for x in data)/den
  assert math.isfinite(value) and value<=limit and abs(value-case['slopes'][key])<1e-9;slopes[key]=value
 assert json.loads((p.parent/'idle-final.json').read_text())['count']==0
 assert status['final_binary_sha256']==json.loads((p.parent/'binary-pins.json').read_text())
 reports.append({'case':case['label'],'cycles':100,'video_frames':frames*100,'pcm_frames':24000000,'slopes':slopes})
assert len(reports)==8 and fresh_audio==400
print(json.dumps({'pass':True,'cells':reports,'cycles':sum(x['cycles'] for x in reports),'video_frames':sum(x['video_frames'] for x in reports),'pcm_frames':sum(x['pcm_frames'] for x in reports),'fresh_consumer_continuity_verified':fresh_audio},indent=2))
