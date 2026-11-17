"""Diagnostic overlay build plus one trace-off/on pair. Stop on any failure."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
import replay_verify as verify
root=Path(__file__).resolve().parent;spec=json.loads((root/'config.json').read_text());src=root/'source';build=root/'build';out=root/'results';out.mkdir(exist_ok=False)
state=dict(revision=spec['revision'],state='preparing',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
def save():(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd,name,timeout=120,cwd=root,required=True):
 state.update(step=name,command=cmd);save()
 with (out/(name+'.log')).open('w') as f:
  p=subprocess.Popen(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=f,stderr=subprocess.STDOUT,timeout=30);p.wait(timeout=30);raise RuntimeError(name+' timeout; owned tree cleanup only')
 state.pop('owned_pid',None);state[name+'_exit']=rc;save()
 if rc and required:raise RuntimeError(name+' failed; no next arm')
 return rc
def source_hashes():return {p.relative_to(src).as_posix():sha(p) for p in src.rglob('*') if p.is_file()}
def tape():
 p=Path(spec['fixture_path']);pin={'path':spec['fixture_path'],'bytes':p.stat().st_size,'sha256':sha(p)}
 if pin!=spec['fixture_pin']:raise RuntimeError('existing tape pin mismatch')
 return pin
def pins():return {n:sha(build/'Release'/n) for n in ['fl_gst_local_decoder_rate_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll']}
save()
try:
 for key in list(os.environ):
  if key.upper().startswith(('FRAMELINK_','FL_BENCH_','FL_FAULT','GST_')):os.environ.pop(key,None)
 os.environ.update(MSBUILDDISABLENODEREUSE='1',GST_REGISTRY=str(root/'registry.bin'))
 os.environ['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+os.environ['PATH']
 state['fixture_initial']=tape();save()
 if sha(root/'source.tar')!=spec['archive_sha256']:raise RuntimeError('archive mismatch')
 src.mkdir();run(['tar','-xf',str(root/'source.tar'),'-C',str(src)],'extract')
 if source_hashes()!=spec['source_pins']:raise RuntimeError('extracted full source mismatch')
 run(['git','-c','core.autocrlf=false','-c','core.eol=lf','apply','--check',str(root/'diagnostic.patch')],'patch-check',cwd=src)
 run(['git','-c','core.autocrlf=false','-c','core.eol=lf','apply',str(root/'diagnostic.patch')],'patch-apply',cwd=src)
 expected=dict(spec['source_pins']);expected['test/fl_gst_local_decoder_rate_win.cpp']=spec['diagnostic_sha256']
 if source_hashes()!=expected:raise RuntimeError('overlay changed unexpected source')
 state['overlay_source_sha256']=expected['test/fl_gst_local_decoder_rate_win.cpp'];save()
 run(['cmake','-S',str(src),'-B',str(build),'-G','Visual Studio 17 2022','-DFRAMELINK_BUILD_TESTS=ON','-DFRAMELINK_BUILD_GST=ON','-DFRAMELINK_BUILD_SAMPLES=ON'],'configure',240)
 run(['cmake','--build',str(build),'--config','Release','--target','fl_gst_local_decoder_rate_win','gstframelink','-j','4','--','/nr:false'],'build',1800)
 state['binary_before']=pins();save()
 bins=build/'Release';os.environ['PATH']=str(bins)+';'+os.environ['PATH']
 inspection=verify.inspect_tape(spec['fixture_path'],spec)
 (out/'tape-inspection.json').write_text(json.dumps(inspection,indent=2)+'\n')
 common=[str(bins/'fl_gst_local_decoder_rate_win.exe'),str(bins/'gstframelink.dll'),'--seconds','5','--width','3840','--height','2160','--fps','30','--format','RGBA','--fixture',spec['fixture_path']]
 for arm in ['trace-off','trace-on']:
  state[arm+'_fixture_before']=tape();state[arm+'_binary_before']=pins();save()
  if pins()!=state['binary_before']:raise RuntimeError('binary changed')
  try:run(common+['--out',str(out/arm)]+(['--trace'] if arm=='trace-on' else []),arm,90)
  finally:
   state[arm+'_fixture_after']=tape();state[arm+'_binary_after']=pins();save()
  if pins()!=state['binary_before']:raise RuntimeError('binary changed')
  # Phase evidence is still analyzed when the unchanged timing gate fails.
  gate=run([sys.executable,'-E',str(root/'check_local_decoder_rate.py'),str(out/arm)],arm+'-analysis',60,required=False)
  phase=run([sys.executable,'-E',str(root/'analyze_phases.py'),str(out/arm/'source.video.csv')],arm+'-phases',60,required=False)
  verify.capture(json.loads((out/arm/'config.json').read_text()),spec,state[arm+'_fixture_after'],arm=='trace-on')
  if gate or phase:raise RuntimeError(arm+' analyzer failed; next arm withheld')
  verify.analysis(json.loads((out/(arm+'-analysis.log')).read_text()),arm=='trace-on')
 state['state']='finished'
except Exception as e:state.update(state='failed',error=str(e))
finally:
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2));sys.exit(0 if state['state']=='finished' else 1)
