#!/usr/bin/env python3
"""Prepare fresh exact-revision Windows build and one short smoke, never endurance."""
from pathlib import Path, PureWindowsPath
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys,tarfile
def collected_path(data, name):
 # Legacy Windows manifests used backslashes. Interpret both separator forms
 # explicitly; reject drives/UNC/rooted paths and traversal before resolution.
 if not isinstance(name,str) or not name or "\0" in name:
  raise RuntimeError('invalid collected manifest path')
 relative=PureWindowsPath(name)
 if relative.anchor or not relative.parts or any(part=='..' or ':' in part for part in relative.parts):
  raise RuntimeError('unsafe collected manifest path: '+name)
 root=data.resolve();path=root.joinpath(*relative.parts).resolve()
 if not path.is_relative_to(root) or path==root:
  raise RuntimeError('collected manifest path escapes root: '+name)
 return path
def verify_collection(data):
 manifest=json.loads((data/'collected-sha256.json').read_text());seen=set()
 if not isinstance(manifest,dict) or not manifest:raise RuntimeError('empty collection manifest')
 for name,digest in manifest.items():
  path=collected_path(data,name)
  if path in seen or not isinstance(digest,str) or not re.fullmatch('[a-f0-9]{64}',digest):raise RuntimeError('duplicate path or invalid collection digest')
  seen.add(path)
  if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('collected hash mismatch: '+name)
 actual={p.resolve() for p in data.rglob('*') if p.is_file() and p!=data/'collected-sha256.json'}
 if actual!=seen:raise RuntimeError('collection manifest does not cover every raw artifact')
def successful(state):
 return not state.get('error') and not state.get('collection_error') and state.get('remote_exit')==0 and state.get('package_exit')==0 and state.get('pull_exit')==0
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
if changes!='samples/native_relay.h' or git('rev-parse',revision+':samples/native_relay.h')!='7dc3d8187747af829690a26a7a72298d8efe0c8e':p.error('only the reviewed native relay drain fix may differ from461584b')
root=base/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False)
remote='<win-home>/dev/framelink-native-ready-'+a.run_id
host='<win-user>@<win-host>'
state={'revision':revision,'remote':remote,'runtime_lane_released':True,'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
def command(args,name,timeout=120):
 with (root/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
def ps(source):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(source.encode('utf-16le')).decode()]
provisioned=False
shutil.copy2(__file__,root/'controller.py')
save()
try:
 files=['remote.py','execute.ps1','collect.py']
 for name in files:shutil.copy2(base/name,root/name)
 subprocess.run(['git','-C',str(repo),'archive','--format=tar','-o',str(root/'source.tar'),revision],check=True)
 archivehash=hashlib.sha256((root/'source.tar').read_bytes()).hexdigest()
 (root/'config.json').write_text(json.dumps({'revision':revision,'production_source':'461584b','reviewed_sample_blob':'7dc3d8187747af829690a26a7a72298d8efe0c8e','archive_sha256':archivehash},indent=2)+'\n')
 files+=['source.tar','config.json']
 (root/'artifact-sha256.json').write_text(json.dumps({name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in files},indent=2)+'\n')
 rc=command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+remote+"' -ErrorAction Stop | Out-Null"),'provision')
 if rc:raise RuntimeError('remote path creation failed; no reuse')
 provisioned=True
 for name in files+['artifact-sha256.json']:
  if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name,240):raise RuntimeError('upload failed: '+name)
 state['state']='running';save()
 state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',1500)
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
     data=root/'data';data.mkdir()
     with tarfile.open(root/'evidence.tar') as archive:archive.extractall(data,filter='data')
     verify_collection(data)
     if state.get('remote_exit')==0:
      status=json.loads((data/'status.json').read_text())
      if status.get('state')!='finished' or status.get('revision')!=revision or any(status.get(k)!=0 for k in ['build_exit','focused-tests_exit','decoder-rate-smoke_exit','decoder-rate-analysis_exit']):raise RuntimeError('remote outcome inconsistent with successful exit')
      if json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:raise RuntimeError('missing or busy final idle receipt')
   else:
    # No smoke directory is expected on a failed build; retain logs even then.
    state['fallback_pull_exit']=command(['scp','-q','-o','BatchMode=yes',host+':'+remote+'/*.log',host+':'+remote+'/*.json',str(root)],'pull-failed',180)
  except Exception as e:state['collection_error']=str(e)
 state['state']='finished' if successful(state) else 'failed'
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
