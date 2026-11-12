from pathlib import Path
import csv,json,re,sys
root=Path(sys.argv[1]);injected=sys.argv[2]=='injected'
rows=list(csv.DictReader((root/'source.phases.csv').open()));rows=[{k:int(v) for k,v in row.items()} for row in rows]
if not rows or [r['identity'] for r in rows]!=list(range(len(rows))):raise ValueError('missing/noncontiguous phase records')
phases={}
for name in ('pace','pause','row','acquire'):
 values=[(r[name+'_end_ns']-r[name+'_begin_ns'],r['identity']) for r in rows if r[name+'_begin_ns'] and r[name+'_end_ns']]
 if any(v<0 for v,i in values):raise ValueError('negative phase interval')
 phases[name]=dict(count=len(values),max_ms=max((v/1e6 for v,i in values),default=0),slowest=sorted([dict(identity=i,ms=v/1e6) for v,i in values],key=lambda x:x['ms'],reverse=True)[:5])
logs={role:(root/(role+'.stderr.log')).read_text() for role in ('source','relay','receiver')}
replacements=[]
for path in root.glob('*.trace.csv.*.csv'):
 with path.open() as f:
  records=list(csv.DictReader(line for line in f if not line.startswith('#')))
 for r in records:
  if r.get('operation')=='ipc.frame_received' and r.get('result')=='1':replacements.append(dict(file=path.name,sequence=r['sequence'],pts_ns=r['pts_ns'],channel_id=r['channel_id'],start_ns=r['start_ns']))
if injected:
 pause=[r for r in rows if r['identity']==60 and r['pause_begin_ns'] and r['pause_end_ns']]
 if len(pause)!=1 or not 300000000<=pause[0]['pause_end_ns']-pause[0]['pause_begin_ns']<=2000000000:raise ValueError('injected pause absent/outside bounded control')
else:
 if len(rows)!=300 or any(r['pause_begin_ns'] for r in rows):raise ValueError('baseline phases incomplete or injected')
print(json.dumps(dict(scope='diagnostic only, not qualification or original-stall attribution',injected=injected,phase_rows=len(rows),phases=phases,queue_replacements=replacements,errors=logs),indent=2))
