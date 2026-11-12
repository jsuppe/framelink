#!/usr/bin/env python3
"""One matched and proven native target cell: 1800 seconds trace-off only."""
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
base=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pair-run',type=Path,required=True,help='Collected matching five-second trace-off/on pair')
p.add_argument('--run-id',required=True)
p.add_argument('--runtime-lane-released',action='store_true',required=True)
a=p.parse_args()
if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid unique run id')
root=base/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False)
pair=a.pair_run.resolve();data=pair/'data'
controller=json.loads((pair/'controller-status.json').read_text())
status=json.loads((data/'status.json').read_text());prior=json.loads((data/'config.json').read_text())
pinned=json.loads((base/'binary-pins.json').read_text())
revision=pinned['revision']
if any(controller.get(k)!=0 for k in ['remote_exit','package_exit','pull_exit']):p.error('pair was not successfully executed and collected')
if status.get('state')!='finished' or any(s.get('revision')!=revision for s in [controller,status,prior]):p.error('pair revision or remote outcome mismatch')
# Recovered collection failures are acceptable only with the explicit recovery receipt;
# raw manifest, config, binaries, idle and both analyses are still revalidated below.
if controller.get('state')!='finished':
 recovery=json.loads((pair/'recovery-receipt.json').read_text())
 if recovery.get('idle',{}).get('activeRuntime')!=[] or any(recovery.get('results',{}).get(arm,{}).get('exit')!=0 for arm in ['trace-off','trace-on']):p.error('unresolved collection failure')
 if any(recovery['results'][arm].get('analysis',{}).get('pass_all') is not True for arm in ['trace-off','trace-on']):p.error('recovery did not validate pair')
binaries=pinned['binary_sha256']
if prior.get('binary_sha256')!=binaries or status.get('binary_before')!=binaries:p.error('pair binary pins differ')
for arm in ['trace-off','trace-on']:
 if status.get(arm+'_exit')!=0 or status.get(arm+'-analysis_exit')!=0 or status.get(arm+'_binary_after')!=binaries:p.error('pair arm incomplete or binary changed')
if json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:p.error('pair final idle receipt missing or busy')
for name,digest in json.loads((data/'collected-sha256.json').read_text()).items():
 if hashlib.sha256(collected_path(data,name).read_bytes()).hexdigest()!=digest:p.error('pair collected hash mismatch: '+name)
repo=Path('<linux-home>/dev/framelink-m6')
analyzer=subprocess.check_output(['git','-C',str(repo),'show',revision+':tools/check_local_native_bench.py'])
(root/'check_local_native_bench.py').write_bytes(analyzer)
for arm in ['trace-off','trace-on']:
 cfg=json.loads((data/arm/'config.json').read_text())
 if cfg.get('seconds')!=5 or cfg.get('trace')!=(arm=='trace-on') or any(cfg.get(k)!=prior.get(k) for k in ['width','height','fps','format']):p.error('pair profiles do not match')
 with (root/(arm+'-prerequisite-analysis.log')).open('w') as log:
  rc=subprocess.run([sys.executable,str(root/'check_local_native_bench.py'),str(data/arm)],stdout=log,stderr=subprocess.STDOUT,timeout=60).returncode
 result=json.loads((root/(arm+'-prerequisite-analysis.log')).read_text())
 if rc or result.get('pass_all') is not True or result.get('trace_checked')!=(arm=='trace-on'):p.error('raw pair analysis failed: '+arm)
if (prior['width'],prior['height'],prior['fps']) not in [(1920,1080,60),(3840,2160,30)] or prior['format'] not in ['BGRA','NV12']:p.error('unsupported target profile')
if not re.fullmatch(r'<win-home>/dev/[a-zA-Z0-9_-]+/build/Release',prior['binary_directory']):p.error('unexpected remote binary path')
remote='<win-home>/dev/framelink-native-duration-'+a.run_id;host='<win-user>@<win-host>'
spec={k:prior[k] for k in ['revision','binary_directory','binary_sha256','format','width','height','fps']}
spec.update(seconds=1800,pair_run=str(pair),runtime_lane_released=True)
(root/'config.json').write_text(json.dumps(spec,indent=2)+'\n')
state=dict(revision=revision,remote=remote,state='preparing',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
def command(args,name,timeout=120):
 with (root/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
def ps(s):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(s.encode('utf-16le')).decode()]
provisioned=False;save()
try:
 files=['remote.py','execute.ps1','collect.py']
 for name in files:shutil.copy2(base/name,root/name)
 files+=['config.json','check_local_native_bench.py']
 (root/'artifact-sha256.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files},indent=2)+'\n')
 if command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+remote+"' -ErrorAction Stop | Out-Null"),'provision'):raise RuntimeError('remote path exists or could not be created')
 provisioned=True
 for name in files+['artifact-sha256.json']:
  if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name):raise RuntimeError('upload failed: '+name)
 state['state']='running';save()
 state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',2400)
except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:
 if provisioned:
  try:
   state['package_exit']=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package',120)
   if state['package_exit']==0:
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',240)
    if state['pull_exit']==0:
     data=root/'data';data.mkdir()
     with tarfile.open(root/'evidence.tar') as archive:archive.extractall(data,filter='data')
     hashes=json.loads((data/'collected-sha256.json').read_text())
     for name,digest in hashes.items():
      path=collected_path(data,name)
      if not path.is_relative_to(data.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('collected hash mismatch: '+name)
     for arm in ['duration']:
      rc=command([sys.executable,str(root/'check_local_native_bench.py'),str(data/arm)],arm+'-local-analysis',180)
      state[arm+'_local_analysis_exit']=rc
      if rc:raise RuntimeError(arm+' independent local analysis failed')
     idle=json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig'))
     if idle.get('activeRuntime')!=[]:raise RuntimeError('final idle receipt missing or busy')
   else:raise RuntimeError('evidence packaging failed')
  except Exception as e:state['collection_error']=str(e)
 state['state']='finished' if not state.get('error') and not state.get('collection_error') and state.get('remote_exit')==0 and state.get('pull_exit')==0 else 'failed'
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
