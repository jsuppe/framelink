#!/usr/bin/env python3
"""Separate implementation of count/cadence/arrival/resource qualification checks."""
from pathlib import Path
import csv,json,math,statistics,sys
folder=Path(sys.argv[1]);seconds=int(sys.argv[2]);label=folder.name
def read(s):return [{k:int(v) for k,v in row.items()} for row in csv.DictReader((folder/(label+s)).open())]
def distribution(v):
 v=sorted(v);return {'p95_ms':v[math.ceil(.95*len(v))-1]/1e6,'max_ms':v[-1]/1e6,'misses_34ms':sum(x>34000000 for x in v)}
result={'seconds':seconds,'errors':[],'scope':'transport timing and accounting, not payload hashes, physical playback or native identity'}
def require(ok,msg):
 if not ok:result['errors'].append(msg)
offsets=[]
for track,rate,ps,cs in [('video',30,'-producer.txt.push.csv','-consumer.txt.video.csv'),('audio',100,'-producer.txt.audio-push.csv','-consumer.txt.audio.csv')]:
 producer,consumer=read(ps),read(cs);expected=seconds*rate
 require(len(producer)==len(consumer)==expected,track+' counts')
 if not producer or not consumer:continue
 for side,rows in [('producer',producer),('consumer',consumer)]:
  first=rows[0]['pts_ns'];pts=[r['pts_ns'] for r in rows]
  require(len(set(pts))==len(pts),track+' '+side+' duplicate pts')
  require(all(abs((p-first)-i*1e9/rate)<=1.1 for i,p in enumerate(pts)),track+' '+side+' exact configured cadence')
 offset=consumer[0]['pts_ns']-producer[0]['pts_ns'];offsets.append(offset)
 require(all(c['pts_ns']-p['pts_ns']==offset for p,c in zip(producer,consumer)),track+' constant mapping')
 arrival=distribution([r['arrival_lateness_ns'] for r in consumer]);require(arrival['misses_34ms']==0,track+' deadline misses')
 result[track]={'producer_rows':len(producer),'consumer_rows':len(consumer),'pts_offset_ns':offset,'arrival':arrival,'render':distribution([r['render_lateness_ns'] for r in consumer]),'push':distribution([r['push_lateness_ns'] for r in producer])}
require(len(offsets)==2 and offsets[0]==offsets[1],'relative A/V mapping');result['av_mapping_delta_ns']=offsets[-1]-offsets[0] if len(offsets)==2 else None
fields=dict(l.split('=',1) for l in (folder/(label+'.summary.txt')).read_text().splitlines() if '=' in l)
for key,val in {'complete':'yes','consumer.frames':str(seconds*30),'producer.pushed.frames':str(seconds*30),'consumer.samples':str(seconds*48000),'producer.pushed.samples':str(seconds*48000),'consumer.result':'EOS','producer.result':'EOS','consumer.audio.contiguous':'yes','consumer.queue.accounting':'complete','exit':'0/0','video.pool':'2','video.queue':'1','output.queue_buffers':'1','output.async':'false','output.sync':'false','playback.latency_ms':'0','arrival.budget_ms':'34'}.items():require(fields.get(key)==val,key+' mismatch')
result['pcm_sample_frames']=int(fields['consumer.samples']);result['resource_limits_applicable']=seconds>=600
result['resources']={}
for side in ['consumer','producer']:
 rows=[[float(v) for v in row.split(':')] for row in fields[side+'.series.t_cpums_memkb_handles_threads'].strip(';').split(';')]
 steady=[row for row in rows[:-1] if row[0]>=2];half=steady[len(steady)//2:]
 require(len(half)>=2 and half[-1][0]>half[0][0],side+' sufficient resource observations')
 def slope(col):
  xs=[r[0] for r in half];ys=[r[col] for r in half];mx,my=statistics.mean(xs),statistics.mean(ys)
  den=sum((x-mx)**2 for x in xs);return 60*sum((x-mx)*(y-my) for x,y in zip(xs,ys))/den if den else 0
 resource={'samples':len(rows),'cpu_percent':(steady[-1][1]-steady[0][1])/(steady[-1][0]-steady[0][0])/10,'reported_cpu_percent':float(fields[side+'.cpu.percent'])}
 for col,key,limit in [(2,'memory_kb_per_min',64),(3,'handles_per_min',1),(4,'threads_per_min',.5)]:
  resource[key]=slope(col)
  if seconds>=600:require(math.isfinite(resource[key]) and resource[key]<=limit,side+' '+key+' exceeded')
 result['resources'][side]=resource
result['pass']=not result['errors'];print(json.dumps(result,indent=2));sys.exit(0 if result['pass'] else 1)
