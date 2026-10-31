#!/usr/bin/env python3
"""Matched native video transport rate comparison; not scanout measurement."""
import subprocess,time,pathlib,json,os,hashlib
root=pathlib.Path('C:/Users/jonsu/dev/m5-rate-evidence');root.mkdir(exist_ok=True)
bases={'m4':pathlib.Path('C:/Users/jonsu/dev/framelink-m4-20261030/build-final/Release'),'m5':pathlib.Path('C:/Users/jonsu/dev/framelink-m5-pipeline/build/Release')}
summary=[]
for fmt in ['bgra','nv12']:
 for fps in [30,60]:
  for label,b in bases.items():
   name=f'{label}-{fmt}-{fps}';d=root/name;d.mkdir(exist_ok=True);channel='rate-'+name;count=fps*5
   common=['--channel',channel,'--size','1920x1080','--format',fmt,'--frames',str(count),'--deadline-ms','12000']
   env=dict(os.environ,FRAMELINK_TRACE=str(d/'trace'))
   ccmd=[str(b/'framelink_consumer.exe'),*common,'--gpu-sync','1','--verify','1','--map-cpu','0'];pcmd=[str(b/'framelink_producer.exe'),*common,'--fps',str(fps)]
   start=time.perf_counter()
   with (d/'consumer.log').open('w') as co,(d/'producer.log').open('w') as po:
    c=subprocess.Popen(ccmd,stdout=co,stderr=subprocess.STDOUT,env=env);time.sleep(.2);p=subprocess.Popen(pcmd,stdout=po,stderr=subprocess.STDOUT,env=env)
    try:p.wait(timeout=25);c.wait(timeout=25)
    finally:
     for q in [p,c]:
      if q.poll() is None:q.kill();q.wait()
   row=dict(case=name,wall_seconds=time.perf_counter()-start,exit_codes=[p.returncode,c.returncode],consumer=(d/'consumer.log').read_text(),producer=(d/'producer.log').read_text(),commands=[pcmd,ccmd],hashes={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in b.glob('framelink*') if x.suffix in ['.dll','.exe']})
   summary.append(row);(root/'summary.json').write_text(json.dumps(summary,indent=2));print(name,row['exit_codes'],row['consumer'].splitlines()[-1],flush=True)
