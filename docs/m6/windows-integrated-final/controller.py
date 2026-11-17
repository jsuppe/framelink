#!/usr/bin/env python3
"""Future-only exact revision Windows integrated validation. Never executes on import."""
from pathlib import Path,PureWindowsPath
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys,tarfile
import verify
BASE=Path(__file__).resolve().parent
REPO=Path('<linux-home>/dev/framelink-m6')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--revision',required=True);p.add_argument('--run-id',required=True);p.add_argument('--runtime-lane-released',action='store_true',required=True);a=p.parse_args()
 if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('unique lowercase run id required')
 revision=subprocess.check_output(['git','-C',str(REPO),'rev-parse',a.revision+'^{commit}'],text=True).strip()
 subprocess.run(['git','-C',str(REPO),'merge-base','--is-ancestor','462fa5f',revision],check=True)
 public={l for l in subprocess.check_output(['git','-C',str(REPO),'diff','--name-only','a50841a',revision,'--','include','gst/framelink_session_transport.h','gst/framelink_session_context.h','gst/framelink_memory_policy.h'],text=True).split()}
 if not public<={'include/framelink_d3d11.h','gst/framelink_session_transport.h','gst/framelink_memory_policy.h'}:p.error('unreviewed public header change since a50841a: '+str(sorted(public)))
 root=BASE/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False);remote='<win-home>/dev/framelink-final-'+a.run_id;host='<win-user>@<win-host>'
 state={'revision':revision,'state':'preparing','remote':remote,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
 def command(cmd,name,timeout=120):
  with (root/(name+'.log')).open('w') as f:return subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
 def ps(code):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()]
 save();provisioned=False
 shutil.copy2(__file__,root/'controller.py');(root/'invocation.json').write_text(json.dumps(sys.argv,indent=2)+'\n')
 try:
  source=root/'source.tar';subprocess.run(['git','-C',str(REPO),'archive','--format=tar','-o',str(source),revision],check=True)
  pins={}
  with tarfile.open(source) as t:
   for member in t.getmembers():
    if member.isfile() and (member.name=='CMakeLists.txt' or member.name.startswith(('src/','include/','gst/','samples/','test/','tools/','runtime/'))):pins[member.name]=hashlib.sha256(t.extractfile(member).read()).hexdigest()
  config={'revision':revision,'archive_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'source_pins':pins,'required_helper_commit':'462fa5f','profile':'final M6 API candidate: fresh Release all-targets build, full serial CTest, API 128+9 owner exports, C11 owner-header compile, unchanged 7b8a274 clients'}
  (root/'config.json').write_text(json.dumps(config,indent=2)+'\n')
  files=['remote.py','execute.ps1','collect.py','verify.py','references.json']
  for name in files:shutil.copyfile(BASE/name,root/name)
  files+=['source.tar','config.json'];(root/'artifact-sha256.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files},indent=2)+'\n')
  assert command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory '"+remote+"' -ErrorAction Stop | Out-Null"),'provision')==0,'remote destination unavailable'
  provisioned=True
  for name in files+['artifact-sha256.json']:
   assert command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name,240)==0,'upload failed '+name
  state['state']='running';save()
  state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',14400)
 except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
 finally:
  if provisioned:
   try:
    state['package_exit']=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package',120)
    if state['package_exit']!=0:raise RuntimeError('packaging failed')
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',240)
    if state['pull_exit']!=0:raise RuntimeError('download failed')
    data=root/'data';data.mkdir()
    with tarfile.open(root/'evidence.tar') as t:t.extractall(data,filter='data')
    verify.verify_collection(data)
    # Tie downloaded evidence to this invocation, not merely to its own manifest.
    for name in ['config.json','references.json','artifact-sha256.json','remote.py','execute.ps1','collect.py','verify.py']:
     if (data/name).read_bytes()!=(root/name).read_bytes():raise RuntimeError('uploaded artifact differs in receipt: '+name)
    state['verified_files']=len(json.loads((data/'collected-sha256.json').read_text()))
    idle=json.loads((data/'host/idle-final.json').read_text(encoding='utf-8-sig'));state['idle_verified']=idle.get('activeRuntime')==[]
    r=data/'results';refs=json.loads((root/'references.json').read_text());state['remote_status']=verify.receipt_status(r,revision,refs)
    coverage=verify.ctest(r/'inventory.log',r/'full.junit.xml');(root/'local-ctest-coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
    if state['remote_status'].get('expected_tests')!=coverage['expected_count']:raise RuntimeError('remote inventory count mismatch')
    api=verify.api(r,refs);(root/'local-api-comparison.json').write_text(json.dumps(api,indent=2)+'\n')
    compatibility=verify.compatibility(r,refs);(root/'local-compatibility.json').write_text(json.dumps(compatibility,indent=2)+'\n')
    state['local_checks_pass']=coverage['pass'] and api['pass'] and compatibility['pass'] and state['idle_verified']
   except Exception as e:state['collection_error']=str(e)
  state['state']='finished' if state.get('remote_exit')==0 and state.get('local_checks_pass') and not state.get('error') and not state.get('collection_error') else 'failed'
  state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
 print(json.dumps(state,indent=2));return 0 if state['state']=='finished' else 1
if __name__=='__main__':sys.exit(main())
