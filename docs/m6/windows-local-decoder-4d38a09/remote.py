"""Bounded focused build/smoke; invoked only by the exclusive-lane wrapper."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parent
spec=json.loads((root/'config.json').read_text())
state={'revision':spec['revision'],'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'state':'preparing'}
def save():
 (root/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(args,name,timeout):
 state['step']=name;state['command']=args;save()
 with (root/(name+'.log')).open('w') as output:
  p=subprocess.Popen(args,cwd=root,stdout=output,stderr=subprocess.STDOUT)
  state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=output,stderr=subprocess.STDOUT,timeout=30)
   p.wait(timeout=30)
   raise RuntimeError(name+' exceeded its bounded timeout; only owned tree stopped')
 state[name+'_exit']=rc;state.pop('owned_pid',None);save()
 if rc:raise RuntimeError(name+' failed; evidence retained')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
save()
try:
 assert sha(root/'source.tar')==spec['archive_sha256'],'source archive hash mismatch'
 env=os.environ
 for key in list(env):
  if 'TRACE' in key or key.startswith(('FL_BENCH_','FRAMELINK_FAULT','FL_FAULT')):env.pop(key,None)
 env['MSBUILDDISABLENODEREUSE']='1'
 env['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH']
 env['GST_REGISTRY']=str(root/'gst-registry.bin')
 run(['tar','-xf','source.tar'],'extract',120)
 run(['cmake','-S','.','-B','build','-G','Visual Studio 17 2022','-DFRAMELINK_BUILD_TESTS=ON','-DFRAMELINK_BUILD_GST=ON'],'configure',180)
 run(['cmake','--build','build','--config','Release','--target','fl_gst_local_decoder_win','gstframelink','-j','4','--','/nr:false'],'build',600)
 run(['ctest','--test-dir','build','-C','Release','-N','--show-only=json-v1','-R','^gst_local_decoder_discovery_win$'],'inventory',60)
 inventory=json.loads((root/'inventory.log').read_text())
 assert [test['name'] for test in inventory['tests']]==['gst_local_decoder_discovery_win'],'new smoke test not registered exactly once'
 binaries=root/'build'/'Release'
 state['binary_sha256']={name:sha(binaries/name) for name in ['fl_gst_local_decoder_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll']};save()
 env['PATH']=str(binaries)+';'+env['PATH']
 run([str(binaries/'fl_gst_local_decoder_win.exe'),str(binaries/'gstframelink.dll'),'--out',str(root/'smoke')],'smoke',45)
 run([sys.executable,'test/fl_gst_local_decoder_check.py','--check-only',str(root/'smoke')],'analysis',45)
 state['state']='finished'
except Exception as e:
 state['state']='failed';state['error']=str(e)
finally:
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
