"""Bounded focused build/smoke; invoked only by the exclusive-lane wrapper."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys,shutil
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
 run(['cmake','--build','build','--config','Release','--target','fl_gst_native_producer_win','fl_gst_local_decoder_rate_win','fl_native_relay_win','fl_win_sha256_rows','gstframelink','-j','4','--','/nr:false'],'build',600)
 run(['ctest','--test-dir','build','-C','Release','-N','--show-only=json-v1','-R','^(gst_relay_local_bin_pause_win|gst_relay_local_bin_paused_cancel_win|gst_relay_local_bin_win|native_relay_win|gst_native_relay_downstream_death_win|gst_relay_local_cancel_downstream_win|gst_relay_local_cancel_upstream_win|gst_relay_local_recovery_win|win_sha256_rows)$'],'inventory',60)
 inventory=json.loads((root/'inventory.log').read_text())
 assert sorted(test['name'] for test in inventory['tests'])==sorted(['gst_relay_local_bin_pause_win','gst_relay_local_bin_paused_cancel_win','gst_relay_local_bin_win','native_relay_win','gst_native_relay_downstream_death_win','gst_relay_local_cancel_downstream_win','gst_relay_local_cancel_upstream_win','gst_relay_local_recovery_win','win_sha256_rows']),'expected nine focused tests'
 binaries=root/'build'/'Release'
 state['native_relay_check_sha256']=sha(binaries/'fl_native_relay_win.exe')
 state['sha256_check_sha256']=sha(binaries/'fl_win_sha256_rows.exe')
 state['binary_sha256']={name:sha(binaries/name) for name in ['fl_gst_native_producer_win.exe','fl_gst_local_decoder_rate_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll']};save()
 env['PATH']=str(binaries)+';'+env['PATH']
 run(['ctest','--test-dir','build','-C','Release','--output-on-failure','-R','^(gst_relay_local_bin_pause_win|gst_relay_local_bin_paused_cancel_win|gst_relay_local_bin_win|native_relay_win|gst_native_relay_downstream_death_win|gst_relay_local_cancel_downstream_win|gst_relay_local_cancel_upstream_win|gst_relay_local_recovery_win|win_sha256_rows)$'],'focused-tests',180)
 run([str(binaries/'fl_gst_local_decoder_rate_win.exe'),str(binaries/'gstframelink.dll'),'--out',str(root/'smoke'),'--seconds','2'],'decoder-rate-smoke',75)
 run([sys.executable,'tools/check_local_decoder_rate.py',str(root/'smoke')],'decoder-rate-analysis',45)
 state['state']='finished'
except Exception as e:
 state['state']='failed';state['error']=str(e)
finally:
 for name in ['LastTest.log','LastTestsFailed.log']:
  source=root/'build'/'Testing'/'Temporary'/name
  if source.exists():shutil.copy2(source,root/name)
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
