"""Independent Python SHA256 and exact sample inventory; no timing threshold claim."""
from pathlib import Path
import csv,hashlib,json,sys,statistics

def analyze(root):
 root=Path(root);result=json.loads((root/'result.json').read_text())
 assert result=={'pass':True,'known_vectors':True,'padding_excluded':True,'iterations':12,'timing_records':120,'digest_bits':256},'result contract'
 pattern=bytes((i*131+(i>>8)*17+29)&255 for i in range(65536));expected={};seen=set()
 methods=['glib-rows','cng-rows-reusable','cng-packed-reusable']
 for name,width,height in [('1080p',1920,1080),('4k',3840,2160)]:
  size=width*height*4;h=hashlib.sha256();n,tail=divmod(size,len(pattern))
  for _ in range(n):h.update(pattern)
  h.update(pattern[:tail]);expected[name]=h.hexdigest()
 with (root/'timings.csv').open() as f:rows=list(csv.DictReader(f))
 for row in rows:
  profile=row['profile'];geometry,layout=profile.split('-');method=row['method'];iteration=int(row['iteration'])
  assert geometry in expected and layout in ('packed','padded') and method in methods[:3 if layout=='packed' else 2]
  assert 0<=iteration<12 and int(row['elapsed_ns'])>0 and row['digest']==expected[geometry]
  key=(profile,method,iteration);assert key not in seen;seen.add(key)
 wanted={(geometry+'-'+layout,method,i) for geometry in expected for layout in ('packed','padded') for method in methods[:3 if layout=='packed' else 2] for i in range(12)}
 assert seen==wanted,'exact 120-sample inventory'
 timings={}
 for row in rows:timings.setdefault(row['profile']+'/'+row['method'],[]).append(int(row['elapsed_ns'])/1e6)
 timings={k:dict(p50_ms=statistics.median(v),min_ms=min(v),max_ms=max(v)) for k,v in timings.items()}
 return {'timings':timings,'pass':True,'samples':len(seen),'python_sha256':expected,'scope':'warm CPU buffer SHA256 feasibility only; no media or qualification'}
if __name__=='__main__':
 try:result=analyze(sys.argv[1])
 except Exception as e:result={'pass':False,'error':str(e)}
 print(json.dumps(result,indent=2));sys.exit(0 if result['pass'] else 1)
