#!/usr/bin/env python3
"""Separate exact media-offset preservation from actual arrival skew."""
import argparse,csv,json,math
p=argparse.ArgumentParser();p.add_argument('prefix');p.add_argument('--frames',type=int,default=3720);p.add_argument('--fps',type=int,default=60);p.add_argument('--hops',type=int,choices=[1,2],required=True);p.add_argument('--audio-offset-ns',type=int,default=37000000);a=p.parse_args()
def read(suffix):
 with open(a.prefix+suffix) as f:return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(f)]
def distribution(v):
 v=sorted(v);return {name:v[max(0,math.ceil(q*len(v))-1)] for name,q in [('min',0),('p50',.5),('p95',.95),('p99',.99),('max',1)]}
video=read('.consumer.csv');audio={r:read('.audio-'+r+'.csv') for r in ['producer','consumer']+(['relay'] if a.hops==2 else [])}
for role,rows in [('video',video),*audio.items()]:
 assert len(rows)==a.frames and [r['identity'] for r in rows]==list(range(a.frames)),(role,'identity/sample loss')
for role,rows in audio.items():
 for v,r in zip(video,rows):
  n=r['identity'];assert r['pts_ns']==n*10**9//a.fps+a.audio_offset_ns and r['pts_ns']-v['pts_ns']==a.audio_offset_ns,(role,'changed media A/V offset')
  assert r['samples']==48000//a.fps and r['duration_ns']==(n+1)*10**9//a.fps-n*10**9//a.fps,(role,'changed samples/duration')
  assert r['source_marker_ns']==audio['producer'][n]['source_marker_ns'] and r['arrival_ns']>=r['source_marker_ns'],(role,'source identity/clock changed')
# Producer arrival_ns is recorded after appsrc push returns, not before enqueue.
# A receiver may run before that return; source_marker_ns is the causal lower bound.
# A relay records its arrival before forwarding, so that ordering is valid.
if 'relay' in audio:
 assert all(c['arrival_ns']>=r['arrival_ns'] for c,r in zip(audio['consumer'],audio['relay'])),'relay arrival causality'
receiver=audio['consumer'];skew=[(r['arrival_ns']-v['arrival_ns'])/1e6 for r,v in zip(receiver,video)];differential=[((r['arrival_ns']-r['source_marker_ns'])-(v['arrival_ns']-v['source_marker_ns']))/1e6 for r,v in zip(receiver,video)]
print(json.dumps({'frames':a.frames,'hops':a.hops,'pcm_sample_frames':sum(r['samples'] for r in receiver),'pcm_channels':2,'pcm_logical_bytes':sum(r['samples']*4 for r in receiver),'producer_and_consumer_av_offset_ns':a.audio_offset_ns,'media_offset_change_ns':0,'audio_minus_video_arrival_ms':distribution(skew),'audio_minus_video_transport_delay_ms':distribution(differential),'physical_output_qualified':False},indent=2))
