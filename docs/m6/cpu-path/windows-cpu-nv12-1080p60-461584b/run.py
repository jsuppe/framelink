from pathlib import Path
import subprocess,json,datetime
ROOT=Path(__file__).resolve().parent
REMOTE='<win-home>/dev/framelink-cpu-nv12-1080p60-461584b'
HOST='<win-user>@<win-host>'
s={'state':'preparing','remote':REMOTE,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(ROOT/'controller-status.json').write_text(json.dumps(s,indent=2))
def cmd(args,name,timeout=120):
 with (ROOT/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
save()
try:
 assert cmd(['ssh',HOST,'powershell','-NoProfile','-Command',f'New-Item -ItemType Directory -Path {REMOTE} -ErrorAction Stop'],'provision')==0
 for name in ['remote.py','m6_raw_timing.py','execute.ps1']:
  assert cmd(['scp','-q',str(ROOT/name),HOST+':'+REMOTE+'/'+name],'copy-'+name)==0
 s['state']='running';save()
 s['remote_exit']=cmd(['ssh','-o','BatchMode=yes',HOST,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',REMOTE+'/execute.ps1'],'remote',2100)
 s['state']='collecting';save()
 s['pull_exit']=cmd(['scp','-qr',HOST+':'+REMOTE,str(ROOT/'data')],'pull',180)
 s['state']='finished' if s['remote_exit']==s['pull_exit']==0 else 'failed'
except Exception as e:s.update(state='failed',error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:s['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(s,indent=2),flush=True)
raise SystemExit(0 if s['state']=='finished' else 1)
