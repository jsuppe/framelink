#!/usr/bin/env python3
"""Standalone SHA256 feasibility comparison; bounded build and one benchmark."""
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
p.add_argument('--run-id',required=True)
p.add_argument('--runtime-lane-released',action='store_true',required=True)
a=p.parse_args()
if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid run id')
root=base/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False)
remote='<win-home>/dev/framelink-hash-compare-'+a.run_id
host='<win-user>@<win-host>'
state={'remote':remote,'runtime_lane_released':True,'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
def command(args,name,timeout=120):
 with (root/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
def ps(source):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(source.encode('utf-16le')).decode()]
provisioned=False
(root/'invocation.json').write_text(json.dumps(sys.argv,indent=2)+'\n')
save()
try:
 files=['remote.py','execute.ps1','collect.py','CMakeLists.txt','hash_compare.cpp','check_results.py']
 for name in files:shutil.copy2(base/name,root/name)
 (root/'config.json').write_text(json.dumps({'iterations':12,'scope':'standalone hash feasibility only'},indent=2)+'\n')
 files+=['config.json']
 (root/'artifact-sha256.json').write_text(json.dumps({name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in files},indent=2)+'\n')
 shutil.copy2(__file__,root/'controller.py')
 rc=command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+remote+"' -ErrorAction Stop | Out-Null"),'provision')
 if rc:raise RuntimeError('remote path creation failed; no reuse')
 provisioned=True
 for name in files+['artifact-sha256.json']:
  if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name,240):raise RuntimeError('upload failed: '+name)
 state['state']='running';save()
 state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',600)
except Exception as e:
 state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:
 if provisioned:
  try:
   # Preserve small control logs and raw benchmark evidence; exclude build products.
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
      if status.get('state')!='finished' or any(status.get(k)!=0 for k in ['configure_exit','build_exit','benchmark_exit','analysis_exit']):raise RuntimeError('remote outcome inconsistent with successful exit')
      for name in ['config.json','artifact-sha256.json']:
       if (data/name).read_bytes()!=(root/name).read_bytes():raise RuntimeError('collected pin mismatch: '+name)
      if status.get('binary_before')!=status.get('binary_after') or not re.fullmatch('[a-f0-9]{64}',status.get('binary_before','')):raise RuntimeError('binary changed or pin missing')
      if command([sys.executable,str(root/'check_results.py'),str(data/'result')],'local-analysis',60):raise RuntimeError('independent local result check failed')
      if json.loads((root/'local-analysis.log').read_text()).get('pass') is not True:raise RuntimeError('local checker did not pass')
      if json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:raise RuntimeError('missing or busy final idle receipt')
   else:
    # No result directory is expected on a failed build; retain logs even then.
    state['fallback_pull_exit']=command(['scp','-q','-o','BatchMode=yes',host+':'+remote+'/*.log',host+':'+remote+'/*.json',str(root)],'pull-failed',180)
  except Exception as e:state['collection_error']=str(e)
 state['state']='finished' if successful(state) else 'failed'
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
