from pathlib import Path
import csv,io,json
base=Path(__file__).parent
results={}
for name in ['device-allocation-isolation-20261112','device-settle-batches-20261112','device-anchor-isolation-20261112']:
 root=base/name;results[name]={}
 for p in sorted(root.glob('*.csv')):
  raw=p.read_bytes();rows=list(csv.DictReader(io.StringIO(raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig'))))
  for r in rows:
   for k in r:
    if k!='phase':r[k]=int(r[k])
  out={}
  for phase in dict.fromkeys(r['phase'] for r in rows):
   a=[r for r in rows if r['phase']==phase];assert all(r['device_release_refs']==0 for r in a)
   x={'count':len(a),'first':a[0],'last':a[-1]}
   if phase.startswith('cycle'):
    assert len(a)==100 and [r['cycle'] for r in a]==list(range(100));v=a[5:];xm=sum(r['cycle'] for r in v)/len(v)
    def slope(k):
     ym=sum(r[k] for r in v)/len(v);return sum((r['cycle']-xm)*(r[k]-ym) for r in v)/sum((r['cycle']-xm)**2 for r in v)/1024
    x['postwarm_KiB_per_cycle']={k:slope(k) for k in ['private_bytes','committed_private','committed_mapped','committed_image']}
   elif phase.startswith('settle'):assert len(a)==30 and [r['cycle'] for r in a]==list(range(1,31))
   else:assert len(a)==1
   out[phase]=x
  expected=391 if 'batches' in name else 163 if 'anchor' in name else 131
  assert len(rows)==expected
  results[name][p.stem]=out
(base/'device-causal-analysis.json').write_text(json.dumps(results,indent=2));print('Verified six serial arms, 800 device cycles, 270 post-teardown samples; diagnostic execution only, no qualification waiver')
