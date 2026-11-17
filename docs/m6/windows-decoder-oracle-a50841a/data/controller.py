#!/usr/bin/env python3
"""Future-only bounded oracle diagnostic; requires successful exact final integrated run."""
from pathlib import Path
import argparse,base64,datetime,hashlib,json,re,shutil,subprocess,sys,tarfile
import integrated_verify as integrated
import replay_verify as verify
import review_checks
BASE=Path(__file__).resolve().parent;REPO=Path('<linux-home>/dev/framelink-m6')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--final-build-run',type=Path,required=True);parser.add_argument('--revision',required=True);parser.add_argument('--run-id',required=True);parser.add_argument('--runtime-lane-released',action='store_true',required=True);a=parser.parse_args()
 if not re.fullmatch('[a-z0-9][a-z0-9-]{0,63}',a.run_id):parser.error('invalid unique run id')
 provenance=json.loads((BASE/'provenance.json').read_text());revision=subprocess.check_output(['git','-C',str(REPO),'rev-parse',a.revision+'^{commit}'],text=True).strip()
 if revision!=provenance['base_revision']:parser.error('reviewed overlay requires exact a50841a baseline')
 prior=a.final_build_run.resolve()
 review_checks.validate_prior(prior,revision,json.loads((BASE/'expected-integrated-references.json').read_text()))
 manifest=json.loads((BASE/'artifact-sha256.json').read_text())
 for n,h in manifest.items():
  if sha(BASE/n)!=h:parser.error('prepared artifact changed: '+n)
 root=BASE/'runs'/a.run_id;root.mkdir(parents=True,exist_ok=False);remote='<win-home>/dev/framelink-oracle-'+a.run_id;host='<win-user>@<win-host>'
 state=dict(revision=revision,remote=remote,state='preparing',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),final_build_run=str(prior))
 def save():(root/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
 def command(cmd,name,timeout=120):
  with (root/(name+'.log')).open('w') as f:return subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
 def ps(code):return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()]
 provisioned=False;save()
 try:
  archive=root/'source.tar';subprocess.run(['git','-C',str(REPO),'archive','--format=tar','-o',str(archive),revision],check=True)
  with tarfile.open(archive) as t:source_pins={m.name:hashlib.sha256(t.extractfile(m).read()).hexdigest() for m in t.getmembers() if m.isfile()}
  if source_pins['test/fl_gst_local_decoder_rate_win.cpp']!=sha(BASE/'original.cpp'):raise RuntimeError('original source mismatch')
  spec=dict(revision=revision,archive_sha256=sha(archive),source_pins=source_pins,diagnostic_sha256=sha(BASE/'diagnostic.cpp'),fixture_path=provenance['fixture']['path'],fixture_pin=provenance['fixture'],seconds=5,width=3840,height=2160,fps=30,format='RGBA')
  (root/'config.json').write_text(json.dumps(spec,indent=2)+'\n')
  (root/'check_local_decoder_rate.py').write_bytes(subprocess.check_output(['git','-C',str(REPO),'show',revision+':tools/check_local_decoder_rate.py']))
  names=['remote.py','execute.ps1','collect.py','replay_verify.py','analyze_phases.py','original.cpp','diagnostic.cpp','diagnostic.patch','provenance.json','README.md','review_checks.py','integrated_verify.py','expected-integrated-references.json']
  for n in names:shutil.copyfile(BASE/n,root/n)
  shutil.copyfile(__file__,root/'controller.py');names+=['config.json','check_local_decoder_rate.py','controller.py','source.tar']
  uploads={n:sha(root/n) for n in names};(root/'artifact-sha256.json').write_text(json.dumps(uploads,indent=2)+'\n')
  if command(ps("$ErrorActionPreference='Stop'; New-Item -ItemType Directory '"+remote+"' -ErrorAction Stop | Out-Null"),'provision'):raise RuntimeError('remote directory unavailable')
  provisioned=True
  for n in names+['artifact-sha256.json']:
   if command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',str(root/n),host+':'+remote+'/'+n],'upload-'+n,240):raise RuntimeError('upload failed '+n)
  state['state']='running';save()
  state['remote_exit']=command(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1','-RuntimeLaneReleased'],'remote',3000)
 except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
 finally:
  if provisioned and not state.get('remote_may_still_run'):
   try:
    state['package_exit']=command(ps("Set-Location '"+remote+"'; python -E collect.py; exit $LASTEXITCODE"),'package')
    if state['package_exit']:raise RuntimeError('packaging failed')
    state['pull_exit']=command(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=15',host+':'+remote+'/evidence.tar',str(root/'evidence.tar')],'pull',240)
    if state['pull_exit']:raise RuntimeError('pull failed')
    collected=root/'data';collected.mkdir()
    with tarfile.open(root/'evidence.tar') as t:t.extractall(collected,filter='data')
    verify.verify_collection(collected)
    if (collected/'artifact-sha256.json').read_bytes()!=(root/'artifact-sha256.json').read_bytes():raise RuntimeError('upload manifest identity mismatch')
    for n,h in uploads.items():
     if n!='source.tar' and sha(collected/n)!=h:raise RuntimeError('upload identity mismatch '+n)
    idle=json.loads((collected/'host/idle-final.json').read_text(encoding='utf-8-sig'));state['final_idle']=idle
    if idle.get('activeRuntime')!=[]:raise RuntimeError('final idle missing/busy')
    results=collected/'results';receipt=json.loads((results/'status.json').read_text());state['remote_status']=receipt
    errors=[]
    if set(receipt.get('binary_before',{}))!={'fl_gst_local_decoder_rate_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll'}:errors.append('four diagnostic binary pins missing')
    if not all(isinstance(h,str) and re.fullmatch('[a-f0-9]{64}',h) for h in receipt.get('binary_before',{}).values()):errors.append('malformed binary hashes')
    if receipt.get('fixture_initial')!=spec['fixture_pin']:errors.append('initial tape mismatch')
    if receipt.get('overlay_source_sha256')!=spec['diagnostic_sha256']:errors.append('diagnostic source pin mismatch')
    for arm in ['trace-off','trace-on']:
     if not (results/arm/'source.video.csv').is_file():raise RuntimeError('missing raw arm: '+arm)
     verify.capture(json.loads((results/arm/'config.json').read_text()),spec,spec['fixture_pin'],arm=='trace-on')
     for suffix,script,target in [('analysis','check_local_decoder_rate.py',results/arm),('phases','analyze_phases.py',results/arm/'source.video.csv')]:
      rc=command([sys.executable,str(root/script),str(target)],arm+'-local-'+suffix,60)
      state[arm+'_local_'+suffix+'_exit']=rc
      if rc:errors.append(arm+' '+suffix)
      elif suffix=='analysis':verify.analysis(json.loads((root/(arm+'-local-'+suffix+'.log')).read_text()),arm=='trace-on')
      else:review_checks.phase_receipt(json.loads((root/(arm+'-local-'+suffix+'.log')).read_text()))
    required=['extract','patch-check','patch-apply','configure','build','trace-off','trace-off-analysis','trace-off-phases','trace-on','trace-on-analysis','trace-on-phases']
    if receipt.get('state')!='finished' or receipt.get('revision')!=revision or any(receipt.get(k+'_exit')!=0 for k in required):errors.append('remote steps incomplete or failed')
    for arm in ['trace-off','trace-on']:
     if receipt.get(arm+'_fixture_before')!=spec['fixture_pin'] or receipt.get(arm+'_fixture_after')!=spec['fixture_pin']:errors.append(arm+' tape pins')
     if receipt.get(arm+'_binary_before')!=receipt.get('binary_before') or receipt.get(arm+'_binary_after')!=receipt.get('binary_before'):errors.append(arm+' binary pins')
    if errors:raise RuntimeError('; '.join(errors))
    state['local_checks_pass']=True
   except Exception as e:state['collection_error']=str(e)
  state['state']='finished' if state.get('remote_exit')==0 and state.get('local_checks_pass') and not state.get('error') and not state.get('collection_error') else 'failed';state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
 print(json.dumps(state,indent=2));return 0 if state['state']=='finished' else 1
if __name__=='__main__':sys.exit(main())
