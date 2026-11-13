from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).resolve().parent;remote='<win-home>/dev/framelink-sharedcpu-lifecycle-bgra1080-consumer-fixed-20261112';host='<win-user>@<win-host>'
s={'state':'preparing','remote':remote,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'controller-status.json').write_text(json.dumps(s,indent=2)+'\n')
def ps(code,name,timeout=120):
 with (root/name).open('w') as f:return subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
save();files=['remote.py','binary-pins.json','execute.ps1'];(root/'artifact-sha256.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files},indent=2)+'\n')
# Remote unique directory already staged and verified by prepare script.
for n in files+['artifact-sha256.json']:subprocess.run(['scp','-q',str(root/n),host+':'+remote+'/'+n],check=True)
s['state']='running';save()
s['remote_exit']=ps("& '"+remote+"/execute.ps1';exit $LASTEXITCODE",'remote.log',1500);s['state']='collecting';save()
subprocess.run(['scp','-qr',host+':'+remote,str(root/'data')],check=True,timeout=240)
s.update(state='collected',ended_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save();print(json.dumps(s),flush=True)
