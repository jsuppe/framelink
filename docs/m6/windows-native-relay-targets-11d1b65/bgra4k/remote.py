"""One fixed five-second target pair. Never builds, retries or runs endurance."""
from pathlib import Path
import datetime, hashlib, json, os, subprocess, sys
root=Path(__file__).resolve().parent
spec=json.loads((root/'config.json').read_text())
state={'revision':spec['revision'],'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save(): (root/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(args,name,timeout):
 state.update(step=name,command=args);save()
 with (root/(name+'.log')).open('w') as out:
  p=subprocess.Popen(args,cwd=root,stdout=out,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try: rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=out,stderr=subprocess.STDOUT,timeout=30)
   p.wait(timeout=30);raise RuntimeError(name+' timeout; stopped only owned process tree')
 state[name+'_exit']=rc;state.pop('owned_pid',None);save()
 if rc: raise RuntimeError(name+' failed; no next arm')
def pins():
 actual={n:hashlib.sha256((Path(spec['binary_directory'])/n).read_bytes()).hexdigest() for n in spec['binary_sha256']}
 if actual!=spec['binary_sha256']:raise RuntimeError('binary pin mismatch')
 return actual
def idle(name):
 script="$p=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$'}); @{utc=[DateTime]::UtcNow.ToString('o'); activeRuntime=@($p | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5; if($p.Count){exit 1}"
 run(['powershell','-NoProfile','-Command',script],name,30)
save()
try:
 env=os.environ
 for key in list(env):
  if 'TRACE' in key or key.startswith(('FL_BENCH_','FRAMELINK_FAULT','FL_FAULT')):env.pop(key,None)
 binaries=Path(spec['binary_directory']);env['PATH']=str(binaries)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH']
 env['GST_REGISTRY']=str(root/'gst-registry.bin')
 state['binary_before']=pins();save()
 for arm in ['trace-off','trace-on']:
  idle(arm+'-idle-before')
  command=[str(binaries/'fl_gst_local_native_bench_win.exe'),str(binaries/'gstframelink.dll'),'--out',str(root/arm),'--seconds','5','--width',str(spec['width']),'--height',str(spec['height']),'--fps',str(spec['fps']),'--format',spec['format']]
  if arm=='trace-on':command+=['--trace']
  run(command,arm,90)
  idle(arm+'-idle-after')
  state[arm+'_binary_after']=pins();save()
  run([sys.executable,'-E',str(root/'check_local_native_bench.py'),str(root/arm)],arm+'-analysis',45)
  analysis=json.loads((root/(arm+'-analysis.log')).read_text())
  if analysis.get('pass_all') is not True or analysis.get('trace_checked') != (arm=='trace-on'):raise RuntimeError('analyzer did not validate expected arm')
  config=json.loads((root/arm/'config.json').read_text())
  for key in ['format','width','height','fps']:
   if config[key]!=spec[key]:raise RuntimeError('capture profile mismatch: '+key)
  if config['seconds']!=5 or config['trace']!=(arm=='trace-on'):raise RuntimeError('capture duration/trace mismatch')
  state[arm+'_analysis']=analysis;save()
 state['state']='finished'
except Exception as e:state.update(state='failed',error=str(e))
finally:state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['state']=='finished' else 1)
