"""Capture missing exe pins under exclusive lane, then one selected native pair."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
import native_verify as verify
root=Path(__file__).resolve().parent;spec=json.loads((root/'config.json').read_text())
state={'revision':spec['revision'],'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(args,name,timeout):
 state.update(step=name,command=args);save()
 with (root/(name+'.log')).open('w') as out:
  p=subprocess.Popen(args,cwd=root,stdout=out,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=out,stderr=subprocess.STDOUT,timeout=30);p.wait(timeout=30);raise RuntimeError(name+' timeout; only owned tree stopped')
 state[name+'_exit']=rc;state.pop('owned_pid',None);save()
 if rc:raise RuntimeError(name+' failed; no next arm')
def read_pins():
 pins={n:hashlib.sha256(Path(spec['artifact_paths'][n]).read_bytes()).hexdigest() for n in sorted(verify.NAMES)}
 verify.pinset(pins,spec['integrated_dll_sha256']);return pins
def pins():
 p=read_pins()
 if p!=state['binary_before']:raise RuntimeError('five binary pins changed')
 return p
def idle(name):
 script="$p=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$'}); @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=@($p | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5; if($p.Count){exit 1}"
 run(['powershell','-NoProfile','-Command',script],name,30)
save()
try:
 for key in list(os.environ):
  if 'TRACE' in key or key.startswith(('FL_BENCH_','FRAMELINK_FAULT','FL_FAULT')):os.environ.pop(key,None)
 bins=Path(spec['binary_directory']);os.environ['PATH']=str(bins)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+os.environ['PATH'];os.environ['GST_REGISTRY']=str(root/'gst-registry.bin')
 idle('pin-idle-before');state['binary_before']=read_pins();save()
 pin_receipt={'revision':spec['revision'],'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'binary_sha256':state['binary_before'],'provenance':'EXE hashes first observed after successful exact integrated build; DLL hashes match integrated before/after pins','artifact_paths':spec['artifact_paths'],'source_archive_sha256':spec['source_archive_sha256']}
 (root/'initial-binary-pins.json').write_text(json.dumps(pin_receipt,indent=2)+'\n')
 for arm in ('trace-off','trace-on'):
  idle(arm+'-idle-before');state[arm+'_binary_before']=pins();save()
  command=[str(bins/'fl_gst_local_native_bench_win.exe'),str(bins/'gstframelink.dll'),'--out',str(root/arm),'--seconds','5','--width',str(spec['width']),'--height',str(spec['height']),'--fps',str(spec['fps']),'--format',spec['format']]+(['--trace'] if arm=='trace-on' else [])
  try:run(command,arm,90)
  finally:state[arm+'_binary_after']=pins();save();idle(arm+'-idle-after')
  run([sys.executable,'-E',str(root/'check_local_native_bench.py'),str(root/arm)],arm+'-analysis',60)
  verify.analysis(json.loads((root/(arm+'-analysis.log')).read_text()),arm=='trace-on')
  verify.capture(json.loads((root/arm/'config.json').read_text()),spec,arm=='trace-on')
 state['state']='finished'
except Exception as e:state.update(state='failed',error=str(e))
finally:state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2));sys.exit(0 if state['state']=='finished' else 1)
