#!/usr/bin/env python3
"""One prepared-tape decoder target: five seconds off, then matched trace on."""
from pathlib import Path
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys,tarfile
import verify
BASE=Path(__file__).resolve().parent
REPO=Path('<linux-home>/dev/framelink-m6')
TAPE_FETCH_LIMIT=64*1024*1024

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--build-run',type=Path,required=True);p.add_argument('--run-id',required=True)
 p.add_argument('--resolution',choices=['1080p60','4k30'],required=True)
 p.add_argument('--runtime-lane-released',action='store_true',required=True);a=p.parse_args()
 if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid unique run id')
 root=BASE/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False)
 shutil.copy2(__file__,root/'controller.py');(root/'invocation.json').write_text(json.dumps(sys.argv,indent=2)+'\n')
 remote='<win-home>/dev/framelink-decoder-replay-'+a.run_id;host='<win-user>@<win-host>'
 state={'state':'preparing','remote':remote,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
 def command(args,name,timeout=120):
  with (root/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
 def ps(s):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(s.encode('utf-16le')).decode()]
 provisioned=False;save()
 try:
  build=a.build_run.resolve();verify.verify_collection(build/'data')
  controller=json.loads((build/'controller-status.json').read_text());status=json.loads((build/'data/status.json').read_text())
  revision,binaries=verify.build_prerequisite(controller,status)
  state['revision']=revision;save()
  remote_build=controller['remote']
  if not re.fullmatch(r'<win-home>/dev/[a-zA-Z0-9_-]+',remote_build):raise RuntimeError('unexpected build path')
  analyzer=subprocess.check_output(['git','-C',str(REPO),'show',revision+':tools/check_local_decoder_rate.py'])
  (root/'check_local_decoder_rate.py').write_bytes(analyzer)
  if command([sys.executable,str(root/'check_local_decoder_rate.py'),str(build/'data/smoke')],'prerequisite-smoke-analysis',60):raise RuntimeError('pinned build smoke analysis failed')
  verify.analysis(json.loads((root/'prerequisite-smoke-analysis.log').read_text()),False)
  width,height,fps={'1080p60':(1920,1080,60),'4k30':(3840,2160,30)}[a.resolution]
  spec=dict(revision=revision,binary_directory=remote_build+'/build/Release',binary_sha256=binaries,format='RGBA',width=width,height=height,fps=fps,seconds=5,build_run=str(build),runtime_lane_released=True,fixture_path=remote+'/fixture.flau')
  (root/'config.json').write_text(json.dumps(spec,indent=2)+'\n')
  files=['remote.py','execute.ps1','collect.py','verify.py']
  for name in files:shutil.copy2(BASE/name,root/name)
  files+=['config.json','check_local_decoder_rate.py']
  (root/'artifact-sha256.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files},indent=2)+'\n')
  if command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+remote+"' -ErrorAction Stop | Out-Null"),'provision'):raise RuntimeError('remote path unavailable; no reuse')
  provisioned=True
  for name in files+['artifact-sha256.json']:
   if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name):raise RuntimeError('upload failed: '+name)
  state['state']='running';save()
  state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',660)
 except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
 finally:
  if provisioned:
   try:
    state['package_exit']=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package',120)
    if state['package_exit']!=0:raise RuntimeError('packaging failed')
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',180)
    if state['pull_exit']!=0:raise RuntimeError('collection failed')
    data=root/'data';data.mkdir()
    with tarfile.open(root/'evidence.tar') as archive:archive.extractall(data,filter='data')
    verify.verify_collection(data)
    for name in files+['artifact-sha256.json']:
     if (data/name).read_bytes()!=(root/name).read_bytes():raise RuntimeError('collected upload differs: '+name)
    status=json.loads((data/'status.json').read_text())
    # Preserve a modest tape even when a measured arm fails. Never regenerate it.
    tape=status.get('fixture_pin') or status.get('fixture_observed_final')
    if tape is not None:
     verify.tape_pin(tape,spec)
     if tape['bytes']<=TAPE_FETCH_LIMIT:
      state['tape_pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+spec['fixture_path'],str(root/'fixture.flau')],'pull-fixture',240)
      if state['tape_pull_exit']!=0:raise RuntimeError('modest fixture download failed')
      verify.file_pin(root/'fixture.flau',tape);state['fixture_local_sha256']=tape['sha256']
      inspected=verify.inspect_tape(root/'fixture.flau',spec);(root/'local-tape-inspection.json').write_text(json.dumps(inspected,indent=2)+'\n')
     else:state['fixture_retained_remote']=tape
    verify.remote_status(status,spec)
    verify.preparation(data/'prepare',spec,tape)
    for arm in ['trace-off','trace-on']:
     if command([sys.executable,str(root/'check_local_decoder_rate.py'),str(data/arm)],arm+'-local-analysis',60):raise RuntimeError(arm+' pinned local analysis failed')
     verify.analysis(json.loads((root/(arm+'-local-analysis.log')).read_text()),arm=='trace-on')
     verify.capture(json.loads((data/arm/'config.json').read_text()),spec,tape,arm=='trace-on')
    if json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:raise RuntimeError('missing/busy final idle')
    state['local_checks_pass']=True
   except Exception as e:state['collection_error']=str(e)
  state['state']='finished' if state.get('remote_exit')==0 and state.get('package_exit')==0 and state.get('pull_exit')==0 and state.get('local_checks_pass') and not state.get('error') and not state.get('collection_error') else 'failed'
  state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
 print(json.dumps(state,indent=2));return 0 if state['state']=='finished' else 1
if __name__=='__main__':sys.exit(main())
