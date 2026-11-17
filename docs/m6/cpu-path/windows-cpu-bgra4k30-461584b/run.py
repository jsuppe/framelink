#!/usr/bin/env python3
"""One frozen 4K30 cell; never starts without explicit runtime ownership release."""
from pathlib import Path
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys
SOURCE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--format',required=True,choices=['BGRA','NV12'])
p.add_argument('--run-id',required=True,help='Unique lowercase letters/digits/hyphens; reused paths reject')
p.add_argument('--runtime-lane-released',action='store_true',required=True)
a=p.parse_args()
if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid run id')
ROOT=SOURCE/'runs'/a.run_id
ROOT.mkdir(parents=True,exist_ok=False)
REMOTE='<win-home>/dev/framelink-4k-461584b-'+a.run_id
HOST='<win-user>@<win-host>'
s={'state':'preparing','remote':REMOTE,'format':a.format,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'runtime_lane_released':True}
def save():(ROOT/'controller-status.json').write_text(json.dumps(s,indent=2)+'\n')
def cmd(args,name,timeout=120):
 with (ROOT/(name+'.log')).open('w') as f:
  return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
def powershell(source):
 return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',HOST,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(source.encode('utf-16le')).decode()]
provisioned=False
save()
try:
 files=['remote.py','m6_raw_timing.py','execute.ps1','analyze_local.py','binary-pins.json']
 for name in files:shutil.copyfile(SOURCE/name,ROOT/name)
 (ROOT/'config.json').write_text(json.dumps({'format':a.format,'revision':'461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff'},indent=2)+'\n');files+=['config.json']
 (ROOT/'artifact-sha256.json').write_text(json.dumps({name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files},indent=2)+'\n')
 # Atomically refuse an existing destination; never reuse another run's evidence.
 rc=cmd(powershell("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+REMOTE+"' -ErrorAction Stop | Out-Null"),'provision')
 if rc:raise RuntimeError('remote destination creation rejected')
 provisioned=True
 for name in files+['artifact-sha256.json']:
  if cmd(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(ROOT/name),HOST+':'+REMOTE+'/'+name],'copy-'+name):raise RuntimeError('upload failed: '+name)
 s['state']='running';save()
 s['remote_exit']=cmd(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',HOST,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',REMOTE+'/execute.ps1'],'remote',2400)
 s['state']='executed';save()
except Exception as e:
 s.update(state='failed',error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:
 if provisioned:
  try:
   s['pull_exit']=cmd(['scp','-qr','-o','BatchMode=yes','-o','ConnectTimeout=15',HOST+':'+REMOTE,str(ROOT/'data')],'pull',240)
  except Exception as e:s['collection_error']=str(e)
 s['state']='finished' if not s.get('error') and s.get('remote_exit')==0 and s.get('pull_exit')==0 else 'failed'
 s['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(s,indent=2),flush=True)
sys.exit(0 if s['state']=='finished' else 1)
