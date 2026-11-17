from pathlib import Path
import subprocess,json,hashlib,base64,datetime,shutil,tarfile
root=Path(__file__).parent;remote='<win-home>/dev/m6-renderer9f74b9d-framelinka50841a-20261112';host='<win-user>@<win-host>'
renderer=Path('<linux-home>/dev/evidence/m6-renderer-provenance/approved-9f74b9d.tar');framelink=Path('<linux-home>/dev/evidence/m6-cpu-path/windows-final-integrated-prepared/runs/final-windows-a50841a-toolpath-20261112t0822z/source.tar')
config={'renderer':'9f74b9d1fae7e13d520bc5d4ae9827ac568704f6','framelink':'a50841aad44fbc9431234a4f326bad036d193bf5','archives':{}}
for n,p in [('renderer',renderer),('framelink',framelink)]:
 pins={}
 with tarfile.open(p) as t:
  for m in t.getmembers():
   if m.isfile():pins[m.name]=hashlib.sha256(t.extractfile(m).read()).hexdigest()
 config['archives'][n]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'files':pins}
(root/'config.json').write_text(json.dumps(config,indent=2))
s={'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'remote':remote,'owner':'qualification_recovery','scope':'isolated new renderer+FrameLink integration build, no qualification reruns'}
def save():(root/'controller.json').write_text(json.dumps(s,indent=2))
def ps(code,name,timeout=60):
 with (root/(name+'.log')).open('w') as f:return subprocess.run(['ssh','-o','BatchMode=yes','-o','ServerAliveInterval=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
save()
assert ps("$ErrorActionPreference='Stop';New-Item -ItemType Directory '"+remote+"' -ErrorAction Stop | Out-Null",'provision')==0
for p,n in [(renderer,'renderer.tar'),(framelink,'framelink.tar'),(root/'config.json','config.json'),(root/'remote.py','remote.py'),(root/'execute.ps1','execute.ps1')]:subprocess.run(['scp','-q',str(p),host+':'+remote+'/'+n],check=True,timeout=240)
s['state']='running';save();print(json.dumps(s),flush=True)
s['exit']=ps("& '"+remote+"/execute.ps1';exit $LASTEXITCODE",'remote',3600)
s['state']='collecting';save()
# Build tree and source archives stay remote; retrieve command logs/status only.
subprocess.run(['scp','-qr',host+':'+remote+'/results',str(root/'data')],check=True,timeout=120)
s.update(state='finished' if s['exit']==0 else 'failed',ended_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save();print(json.dumps(s),flush=True);raise SystemExit(s['exit'])
