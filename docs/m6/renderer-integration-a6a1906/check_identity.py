from pathlib import Path
import csv,json,sys,re
folder=Path(sys.argv[1]);latency=int(sys.argv[2]);expected_count=300;trace=[];footers={}
for p in sorted(folder.glob('*.csv')):
 lines=p.read_text().splitlines();f=[s for s in lines if s.startswith('# dropped_events=')];assert len(f)==1 and f[0].startswith('# dropped_events=0,rejected_threads=0,'),p;footers[p.name]=f[0];trace.extend(csv.DictReader(s for s in lines if not s.startswith('#')))
def select(op):return [r for r in trace if r['operation']==op and int(r['result']) in ([0,1000001003] if op=='av.frame_present_submit' else [0])]
def key(r):return(r['channel_id'],int(r['fence']),int(r['pts_ns']))
send=select('flEndSubmit');recv=select('flAcquireFrame');assert len(send)==len(recv)==expected_count
ch={r['channel_id'] for r in send};assert len(ch)==1;expected={(next(iter(ch)),n+1,n*10**9//60) for n in range(expected_count)}
assert {key(r) for r in send}=={key(r) for r in recv}==expected
names=['av.frame_adopt','av.frame_render_submit','av.frame_gpu_complete','av.frame_present_submit'];stages={n:select(n) for n in names}
for n,rows in stages.items():assert {key(r) for r in rows}==expected,(n,len({key(r) for r in rows}))
serials={n:{(key(r),int(r['duration_ns'])) for r in rows} for n,rows in stages.items() if n!='av.frame_adopt'};assert len({frozenset(x) for x in serials.values()})==1
send_start={key(r):int(r['start_ns']) for r in send};recv_end={key(r):int(r['start_ns'])+int(r['duration_ns']) for r in recv};adopt={key(r):int(r['start_ns']) for r in stages['av.frame_adopt']}
assert all(send_start[k]<=recv_end[k]<=adopt[k] for k in expected),'source/acquire/adopt order'
serial_time={n:{(key(r),int(r['duration_ns'])):int(r['start_ns']) for r in rows} for n,rows in stages.items() if n!='av.frame_adopt'}
assert all(adopt[k[0]]<=serial_time['av.frame_render_submit'][k]<=serial_time['av.frame_gpu_complete'][k] and serial_time['av.frame_render_submit'][k]<=serial_time['av.frame_present_submit'][k] for k in serial_time['av.frame_render_submit']),'render/completion/present order'
assert not [r for r in trace if r['operation'].startswith('av.frame_drop.')]
log=(folder/'renderer.log').read_text();assert f'AV_RING_CONFIG ring_slots=12 pending_slots=6 common_latency_ns={latency*1000000}' in log
records=[json.loads(s) for s in log.splitlines() if s.startswith('{')];complete=[r for r in records if r.get('event')=='playback-complete'];assert len(complete)==1 and complete[0]['audio_blocks']==300 and complete[0]['audio_samples']==240000 and complete[0]['latency_ns']==latency*1000000
summary=json.loads((folder.parent/'summary.json').read_text());assert len(summary)==1 and summary[0]['exit_codes']==[0,0]
result={'pass':True,'expected_frames':300,'matched_transport':300,'stages':{n:{'unique_frames':len({key(r) for r in rows}),'events':len(rows)} for n,rows in stages.items()},'matching_render_serials':len(next(iter(serials.values()))),'trace_footers':footers,'pcm_sample_frames':240000,'audio_blocks':300,'explicit_latency_ms':latency,'ring_slots':12,'pending_slots':6,'physical_scanout_qualified':False,'scope':'finite integrated scheduled playback and identity/GPU/presentation API submission; no isolated-hop timing or physical-output qualification'}
print(json.dumps(result,indent=2))
