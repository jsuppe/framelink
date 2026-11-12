from pathlib import Path
import subprocess,base64,sys
r=Path(__file__).parent;remote='<win-home>/dev/m6-renderer9f74b9d-framelinka50841a-20261112';latency=sys.argv[1];label=sys.argv[2]
for name in ['probe.py','capture.ps1']:subprocess.run(['scp','-q',str(r/name),'<win-user>@<win-host>:'+remote+'/'+name],check=True)
s="& '"+remote+"/capture.ps1' -Latency "+latency+" -Label '"+label+"';exit $LASTEXITCODE"
p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(s.encode('utf-16le')).decode()],capture_output=True,timeout=90);(r/(label+'-ssh.log')).write_bytes(p.stdout+p.stderr)
subprocess.run(['scp','-qr','<win-user>@<win-host>:'+remote+'/results/'+label+'*',str(r/'data')],check=True);print('capture',label,'exit',p.returncode);raise SystemExit(p.returncode)
