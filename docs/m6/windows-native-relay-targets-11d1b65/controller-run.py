#!/usr/bin/env python3
"""One selected native target cell: five seconds off, then five seconds traced."""
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
p.add_argument('--build-run',type=Path,required=True,help='Collected successful exact-revision build/smoke run')
p.add_argument('--run-id',required=True)
p.add_argument('--format',choices=['BGRA','NV12'],required=True)
p.add_argument('--resolution',choices=['1080p60','4k30'],required=True)
p.add_argument('--runtime-lane-released',action='store_true',required=True)
a=p.parse_args()
if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid unique run id')
build=a.build_run.resolve();controller=json.loads((build/'controller-status.json').read_text());status=json.loads((build/'data'/'status.json').read_text())
if controller.get('state')!='finished' or controller.get('remote_exit')!=0 or controller.get('pull_exit')!=0:p.error('build run not successfully collected')
if status.get('state')!='finished' or status.get('build_exit')!=0 or status.get('smoke_exit')!=0:p.error('build or smoke did not pass')
revision=controller['revision']
if status['revision']!=revision or not re.fullmatch('[a-f0-9]{40}',revision):p.error('revision mismatch')
binaries=status['binary_sha256'];required={'fl_gst_local_native_bench_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll'}
if set(binaries)!=required or not all(re.fullmatch('[a-f0-9]{64}',h) for h in binaries.values()):p.error('four exact binary pins required')
remote_build=controller['remote']
if not re.fullmatch(r'<win-home>/dev/[a-zA-Z0-9_-]+',remote_build):p.error('unexpected remote build path')
root=base/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False)
repo=Path('<linux-home>/dev/framelink-m6')
# The analyzer is retrieved from the built commit, never the moving worktree.
analyzer=subprocess.check_output(['git','-C',str(repo),'show',revision+':tools/check_local_native_bench.py'])
(root/'check_local_native_bench.py').write_bytes(analyzer)
with (root/'prerequisite-smoke-analysis.log').open('w') as log:
 rc=subprocess.run([sys.executable,str(root/'check_local_native_bench.py'),str(build/'data'/'smoke')],stdout=log,stderr=subprocess.STDOUT,timeout=45).returncode
if rc or json.loads((root/'prerequisite-smoke-analysis.log').read_text()).get('pass_all') is not True:p.error('collected build smoke does not pass pinned analyzer')
width,height,fps={'1080p60':(1920,1080,60),'4k30':(3840,2160,30)}[a.resolution]
remote='<win-home>/dev/framelink-native-target-'+a.run_id;host='<win-user>@<win-host>'
spec=dict(revision=revision,binary_directory=remote_build+'/build/Release',binary_sha256=binaries,format=a.format,width=width,height=height,fps=fps,seconds=5,build_run=str(build),runtime_lane_released=True)
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
 state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',480)
except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:
 if provisioned:
  try:
   state['package_exit']=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package',120)
   if state['package_exit']==0:
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',180)
    if state['pull_exit']==0:
     data=root/'data';data.mkdir()
     with tarfile.open(root/'evidence.tar') as archive:archive.extractall(data,filter='data')
     hashes=json.loads((data/'collected-sha256.json').read_text())
     for name,digest in hashes.items():
      path=collected_path(data,name)
      if not path.is_relative_to(data.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('collected hash mismatch: '+name)
     for arm in ['trace-off','trace-on']:
      rc=command([sys.executable,str(root/'check_local_native_bench.py'),str(data/arm)],arm+'-local-analysis',45)
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
