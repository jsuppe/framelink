from pathlib import Path
import subprocess,sys,json,time
p=Path(__file__).parent
s={'state':'running','started':time.time(),'scope':'BGRA4K30 consumer packed-plane copy ABBA diagnostic, not qualification','cells':[]}
def save():(p/'campaign-status.json').write_text(json.dumps(s,indent=2))
save()
try:
 for tag,mask in [('a1-row','row'),('b1-plane','plane'),('b2-plane','plane'),('a2-row','row')]:
  s['active']=tag;save()
  with (p/(tag+'-controller.log')).open('w') as f:r=subprocess.run([sys.executable,str(p/'run.py'),'120',tag,mask],stdout=f,stderr=subprocess.STDOUT,timeout=240)
  cell=json.loads((p/(tag+'-status.json')).read_text());s['cells'].append({'tag':tag,'mask':mask,'exit':r.returncode,'state':cell['state']});save()
  assert r.returncode==0 and cell['state']=='finished',tag
 s['state']='finished';s['active']=None
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save()
raise SystemExit(s['state']!='finished')
