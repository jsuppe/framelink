#!/usr/bin/env python3
"""Prepare fresh exact-revision Windows build and one short smoke, never endurance."""
from pathlib import Path
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys
base=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--revision',required=True)
p.add_argument('--run-id',required=True)
p.add_argument('--runtime-lane-released',action='store_true',required=True)
a=p.parse_args()
if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid run id')
repo=Path('<linux-home>/dev/framelink-m6')
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args],text=True).strip()
revision=git('rev-parse',a.revision+'^{commit}')
if git('status','--porcelain'):p.error('source checkout must be clean; commit reviewed harness first')
if git('rev-parse','HEAD')!=revision:p.error('requested revision must be current reviewed HEAD')
# This harness must not silently include new production behavior.
changes=git('diff','--name-only','461584b',revision,'--','src','include','gst','runtime','samples')
if changes:p.error('production source changed since461584b; review and revise campaign explicitly')
root=base/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False)
remote='<win-home>/dev/framelink-local-decoder-'+a.run_id
host='<win-user>@<win-host>'
state={'revision':revision,'remote':remote,'runtime_lane_released':True,'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
def command(args,name,timeout=120):
 with (root/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
def ps(source):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(source.encode('utf-16le')).decode()]
provisioned=False
save()
try:
 files=['remote.py','execute.ps1','collect.py']
 for name in files:shutil.copy2(base/name,root/name)
 subprocess.run(['git','-C',str(repo),'archive','--format=tar','-o',str(root/'source.tar'),revision],check=True)
 archivehash=hashlib.sha256((root/'source.tar').read_bytes()).hexdigest()
 (root/'config.json').write_text(json.dumps({'revision':revision,'production_source':'461584b','archive_sha256':archivehash},indent=2)+'\n')
 files+=['source.tar','config.json']
 (root/'artifact-sha256.json').write_text(json.dumps({name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in files},indent=2)+'\n')
 rc=command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+remote+"' -ErrorAction Stop | Out-Null"),'provision')
 if rc:raise RuntimeError('remote path creation failed; no reuse')
 provisioned=True
 for name in files+['artifact-sha256.json']:
  if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name,240):raise RuntimeError('upload failed: '+name)
 state['state']='running';save()
 state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',1080)
except Exception as e:
 state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:
 if provisioned:
  try:
   # Preserve small control logs and raw smoke evidence; do not copy build/source tar twice.
   rc=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package',120)
   state['package_exit']=rc
   if rc==0:
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',180)
    if state['pull_exit']==0:
     data=root/'data';data.mkdir();subprocess.run(['tar','-xf',str(root/'evidence.tar'),'-C',str(data)],check=True)
   else:
    # No smoke directory is expected on a failed build; retain logs even then.
    state['fallback_pull_exit']=command(['scp','-q','-o','BatchMode=yes',host+':'+remote+'/*.log',host+':'+remote+'/*.json',str(root)],'pull-failed',180)
  except Exception as e:state['collection_error']=str(e)
 state['state']='finished' if not state.get('error') and state.get('remote_exit')==0 and state.get('pull_exit')==0 else 'failed'
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
