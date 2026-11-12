from pathlib import Path
import csv,io,json
root=Path(__file__).parent
out={}
for p in root.glob('*.csv'):
 raw=p.read_bytes();rows=list(csv.DictReader(io.StringIO(raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig'))))
 for r in rows:
  for k in r:
   if k!='phase':r[k]=int(r[k])
 cycle=[r for r in rows if r['phase']=='cycle'];settle=[r for r in rows if r['phase']=='settle'];assert len(cycle)==100 and len(settle)==30 and len(rows)==131
 assert [r['cycle'] for r in cycle]==list(range(100));assert all(r['device_release_refs']==0 for r in cycle)
 def slope(k):
  a=cycle[5:];xm=sum(r['cycle'] for r in a)/len(a);ym=sum(r[k] for r in a)/len(a)
  return sum((r['cycle']-xm)*(r[k]-ym) for r in a)/sum((r['cycle']-xm)**2 for r in a)/1024
 out[p.stem]={'cycles':100,'settle_samples':30,'postwarm_KiB_per_cycle':{k:slope(k) for k in ['private_bytes','committed_private','committed_mapped','committed_image']},'last_cycle':cycle[-1],'last_settle':settle[-1],'settle_private_delta':settle[-1]['private_bytes']-cycle[-1]['private_bytes'],'baseline':rows[0]}
(root/'analysis.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
