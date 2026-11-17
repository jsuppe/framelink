from pathlib import Path
import csv,json,collections
p=Path('<linux-home>/dev/evidence/m6-cost/windows-renderer-all-recorders/results/1920x1080-60-NV12-av');out=Path('<linux-home>/dev/evidence/m6-observability/renderer-join');out.mkdir(exist_ok=True)
def read(name):
 lines=(p/name).read_text().splitlines();foot=[s for s in lines if s.startswith('# dropped_events=')];assert len(foot)==1 and foot[0].startswith('# dropped_events=0,rejected_threads=0,')
 return list(csv.DictReader(s for s in lines if not s.startswith('#')))
a=read('transport.19428.csv');b=read('transport.50968.csv');c=read('renderer.50968.csv')
def select(rows,op):return [r for r in rows if r['operation']==op and r['result']=='0']
def key(r):return (r['channel_id'],int(r['fence']),int(r['pts_ns']))
send=select(a,'flEndSubmit');recv=select(b,'flAcquireFrame');assert len(send)==len(recv)==300
channel={r['channel_id'] for r in send};assert len(channel)==1
expected={(next(iter(channel)),n+1,n*10**9//60) for n in range(300)}
assert {key(r) for r in send}=={key(r) for r in recv}==expected
stages={op:select(c,op) for op in ['av.frame_adopt','av.frame_render_submit','av.frame_present_submit','av.frame_gpu_complete']}
for op,rows in stages.items():assert {key(r) for r in rows}==expected,op
assert len(stages['av.frame_adopt'])==300
first=lambda rows:{k:min(int(r['start_ns']) for r in rows if key(r)==k) for k in expected}
ordered=[first(rows) for rows in [send,recv,stages['av.frame_adopt'],stages['av.frame_render_submit'],stages['av.frame_present_submit']]]
assert all(all(x[k]<=y[k] for k in expected) for x,y in zip(ordered,ordered[1:]))
# Repeated submissions are checked by per-render serial encoded in duration_ns,
# not mistaken for independently fresh source frames.
serials={op:{(key(r),int(r['duration_ns'])) for r in rows} for op,rows in stages.items() if op!='av.frame_adopt'}
assert len({frozenset(v) for v in serials.values()})==1
result={'expected_source_frames':300,'transport_joins':300,'stages':{k:{'events':len(v),'unique_frames':len({key(r) for r in v})} for k,v in stages.items()},'matching_render_serials':len(next(iter(serials.values()))),'trace_loss':0,'physical_scanout_qualified':False,'gpu_completion_semantics':'event labels joined; matching renderer source for fence-completion semantics not yet verified','scope':'legacy100ms hidden-display fixture, not current primary renderer'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
