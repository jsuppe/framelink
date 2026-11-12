#!/usr/bin/env python3
"""One 1800s native trace-off qualification after an exact matching final-build pair."""
from pathlib import Path
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys,tarfile
import integrated_verify as integrated
import prior_checks,native_verify,duration_verify
BASE=Path(__file__).resolve().parent;REPO=Path('<linux-home>/dev/framelink-m6')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pair-run',type=Path,required=True);p.add_argument('--run-id',required=True);p.add_argument('--runtime-lane-released',action='store_true',required=True);a=p.parse_args()
 if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):p.error('invalid unique run id')
 root=BASE/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False);remote='<win-home>/dev/framelink-native-final-duration-'+a.run_id;host='<win-user>@<win-host>'
 state={'state':'preparing','remote':remote,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
 def command(args,name,timeout=120):
  with (root/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
 def ps(code):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()]
 provisioned=False;save();shutil.copy2(__file__,root/'controller.py');(root/'invocation.json').write_text(json.dumps(sys.argv,indent=2)+'\n')
 try:
  pair=a.pair_run.resolve();data=duration_verify.pair_files(pair)
  pair_spec=json.loads((data/'config.json').read_text());duration_verify.profile(pair_spec,5);revision=pair_spec['revision'];state['revision']=revision;save()
  subprocess.run(['git','-C',str(REPO),'merge-base','--is-ancestor','462fa5f',revision],check=True)
  prior=Path(pair_spec['final_build_run']);refs=json.loads((BASE/'expected-integrated-references.json').read_text());proof=prior_checks.validate_prior(prior,revision,refs)
  pc=json.loads((prior/'controller-status.json').read_text());build_remote=pc['remote']
  if not re.fullmatch(r'<win-home>/dev/[a-zA-Z0-9_-]+',build_remote):raise ValueError('unexpected integrated remote root')
  results=prior/'data/results';artifact_paths=native_verify.targets(json.loads((results/'configured-targets.json').read_text()),build_remote+'/build/Release')
  dlls=json.loads((results/'candidate-binaries-after.json').read_text());source_config=json.loads((prior/'data/config.json').read_text())
  analyzer=subprocess.check_output(['git','-C',str(REPO),'show',revision+':tools/check_local_native_bench.py']);analyzer_sha=hashlib.sha256(analyzer).hexdigest()
  if source_config['source_pins'].get('tools/check_local_native_bench.py')!=analyzer_sha or (data/'check_local_native_bench.py').read_bytes()!=analyzer:raise ValueError('analyzer is not exact integrated source')
  pins=duration_verify.pair_receipts(json.loads((pair/'controller-status.json').read_text()),pair_spec,json.loads((data/'status.json').read_text()),json.loads((data/'initial-binary-pins.json').read_text()),artifact_paths,dlls,source_config['archive_sha256'],analyzer_sha)
  (root/'check_local_native_bench.py').write_bytes(analyzer)
  for arm in ('trace-off','trace-on'):
   native_verify.capture(json.loads((data/arm/'config.json').read_text()),pair_spec,arm=='trace-on')
   if command([sys.executable,str(root/'check_local_native_bench.py'),str(data/arm)],arm+'-prerequisite-analysis',60):raise ValueError('raw pair analysis failed: '+arm)
   native_verify.analysis(json.loads((root/(arm+'-prerequisite-analysis.log')).read_text()),arm=='trace-on')
  proof.update(pair_run=str(pair),binary_sha256=pins,profile={k:pair_spec[k] for k in ('width','height','fps','format')},pair_controller_sha256=hashlib.sha256((pair/'controller-status.json').read_bytes()).hexdigest(),pair_manifest_sha256=hashlib.sha256((data/'collected-sha256.json').read_bytes()).hexdigest())
  (root/'prerequisite-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
  spec={k:pair_spec[k] for k in ('revision','binary_directory','artifact_paths','integrated_dll_sha256','source_archive_sha256','source_analyzer_sha256','format','width','height','fps','final_build_run')};spec.update(seconds=1800,binary_sha256=pins,pair_run=str(pair),runtime_lane_released=True)
  (root/'config.json').write_text(json.dumps(spec,indent=2)+'\n')
  files=['remote.py','execute.ps1','collect.py','native_verify.py','duration_verify.py','integrated_verify.py','prior_checks.py','expected-integrated-references.json']
  for name in files:shutil.copy2(BASE/name,root/name)
  files+=['config.json','check_local_native_bench.py','controller.py','prerequisite-proof.json','invocation.json','trace-off-prerequisite-analysis.log','trace-on-prerequisite-analysis.log']
  (root/'artifact-sha256.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files},indent=2)+'\n')
  if command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path '"+remote+"' -ErrorAction Stop | Out-Null"),'provision'):raise RuntimeError('remote path exists/unavailable')
  provisioned=True
  for name in files+['artifact-sha256.json']:
   if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/name),host+':'+remote+'/'+name],'upload-'+name):raise RuntimeError('upload failed: '+name)
  state['state']='running';save()
  state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1'],'remote',2400)
 except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
 finally:
  if provisioned and not state.get('remote_may_still_run'):
   try:
    state['package_exit']=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package',180)
    if state['package_exit']!=0:raise RuntimeError('packaging failed')
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',300)
    if state['pull_exit']!=0:raise RuntimeError('collection failed')
    data=root/'data';data.mkdir()
    with tarfile.open(root/'evidence.tar') as archive:archive.extractall(data,filter='data')
    integrated.verify_collection(data)
    for name in files+['artifact-sha256.json']:
     if (data/name).read_bytes()!=(root/name).read_bytes():raise RuntimeError('downloaded upload differs: '+name)
    duration_verify.duration_receipt(json.loads((data/'status.json').read_text()),spec)
    if command([sys.executable,str(root/'check_local_native_bench.py'),str(data/'duration')],'duration-local-analysis',180):raise RuntimeError('independent duration analysis failed')
    native_verify.analysis(json.loads((root/'duration-local-analysis.log').read_text()),False)
    native_verify.capture(json.loads((data/'duration/config.json').read_text()),spec,False)
    if json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:raise RuntimeError('missing/busy final idle')
    state['local_checks_pass']=True;state['binary_sha256']=pins
   except Exception as e:state['collection_error']=str(e)
  state['state']='finished' if state.get('remote_exit')==0 and state.get('package_exit')==0 and state.get('pull_exit')==0 and state.get('local_checks_pass') and not state.get('error') and not state.get('collection_error') else 'failed'
  state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
 print(json.dumps(state,indent=2));return 0 if state['state']=='finished' else 1
if __name__=='__main__':sys.exit(main())
