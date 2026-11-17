"""Prepare exactly one tape, then one bounded replay pair. No retries."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
import verify
root=Path(__file__).resolve().parent;spec=json.loads((root/'config.json').read_text())
state={'revision':spec['revision'],'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(args,name,timeout):
 state.update(step=name,command=args);save()
 with (root/(name+'.log')).open('w') as out:
  p=subprocess.Popen(args,cwd=root,stdout=out,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=out,stderr=subprocess.STDOUT,timeout=30);p.wait(timeout=30)
   raise RuntimeError(name+' timeout; stopped only owned process tree')
 state[name+'_exit']=rc;state.pop('owned_pid',None);save()
 if rc:raise RuntimeError(name+' failed; no next phase')
def pins():
 actual={n:verify.hash_file(Path(spec['binary_directory'])/n)['sha256'] for n in spec['binary_sha256']}
 if actual!=spec['binary_sha256']:raise RuntimeError('binary pin mismatch')
 return actual
def tape():return dict(path=spec['fixture_path'],**verify.hash_file(spec['fixture_path']))
def idle(name):
 script="$p=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$'}); @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=@($p | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5; if($p.Count){exit 1}"
 run(['powershell','-NoProfile','-Command',script],name,30)
save()
try:
 for key in list(os.environ):
  if 'TRACE' in key or key.startswith(('FL_BENCH_','FRAMELINK_FAULT','FL_FAULT')):os.environ.pop(key,None)
 bins=Path(spec['binary_directory']);os.environ['PATH']=str(bins)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+os.environ['PATH'];os.environ['GST_REGISTRY']=str(root/'gst-registry.bin')
 state['binary_before']=pins();save()
 common=[str(bins/'fl_gst_local_decoder_rate_win.exe'),str(bins/'gstframelink.dll'),'--seconds','5','--width',str(spec['width']),'--height',str(spec['height']),'--fps',str(spec['fps']),'--format','RGBA']
 idle('prepare-idle-before')
 try:run(common+['--out',str(root/'prepare'),'--prepare-fixture',spec['fixture_path']],'prepare',90)
 finally:state['prepare_binary_after']=pins();save();idle('prepare-idle-after')
 state['fixture_pin']=tape();save();verify.preparation(root/'prepare',spec,state['fixture_pin'])
 inspection=verify.inspect_tape(spec['fixture_path'],spec)
 if any(inspection[k]!=state['fixture_pin'][k] for k in ('bytes','sha256')):raise RuntimeError('tape inspection hash differs')
 (root/'tape-inspection.json').write_text(json.dumps(inspection,indent=2)+'\n')
 for arm in ('trace-off','trace-on'):
  idle(arm+'-idle-before');state[arm+'_binary_before']=pins();state[arm+'_fixture_before']=tape();save()
  if state[arm+'_fixture_before']!=state['fixture_pin']:raise RuntimeError('tape changed before arm')
  command=common+['--out',str(root/arm),'--fixture',spec['fixture_path']]+(['--trace'] if arm=='trace-on' else [])
  try:run(command,arm,90)
  finally:
   state[arm+'_fixture_after']=tape();state[arm+'_binary_after']=pins();save();idle(arm+'-idle-after')
  if state[arm+'_fixture_after']!=state['fixture_pin']:raise RuntimeError('tape changed after arm')
  run([sys.executable,'-E',str(root/'check_local_decoder_rate.py'),str(root/arm)],arm+'-analysis',60)
  verify.analysis(json.loads((root/(arm+'-analysis.log')).read_text()),arm=='trace-on')
  verify.capture(json.loads((root/arm/'config.json').read_text()),spec,state['fixture_pin'],arm=='trace-on')
 state['state']='finished'
except Exception as e:state.update(state='failed',error=str(e))
finally:
 if Path(spec['fixture_path']).is_file():state['fixture_observed_final']=tape()
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2));sys.exit(0 if state['state']=='finished' else 1)
