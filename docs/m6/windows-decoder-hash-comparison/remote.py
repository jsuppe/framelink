"""Exclusive-lane bounded standalone build and one CPU-only benchmark."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parent
state={'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save():(root/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(args,name,timeout):
 state.update(step=name,command=args);save()
 with (root/(name+'.log')).open('w') as output:
  p=subprocess.Popen(args,cwd=root,stdout=output,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=output,stderr=subprocess.STDOUT,timeout=30)
   p.wait(timeout=30);raise RuntimeError(name+' timeout; only owned process tree stopped')
 state[name+'_exit']=rc;state.pop('owned_pid',None);save()
 if rc:raise RuntimeError(name+' failed; no retry')
def idle(name):
 script="$p=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$'}); @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=@($p | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5; if($p.Count){exit 1}"
 run(['powershell','-NoProfile','-Command',script],name,30)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
save()
try:
 env=os.environ
 for key in list(env):
  if 'TRACE' in key or key.startswith(('FL_BENCH_','FRAMELINK_FAULT','FL_FAULT')):env.pop(key,None)
 env['MSBUILDDISABLENODEREUSE']='1';env['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH']
 run(['cmake','-S','.','-B','build','-G','Visual Studio 17 2022','-A','x64'],'configure',90)
 run(['cmake','--build','build','--config','Release','--target','fl_hash_compare','-j','2','--','/nr:false'],'build',180)
 exe=root/'build'/'Release'/'fl_hash_compare.exe';state['binary_before']=sha(exe);save()
 idle('benchmark-idle-before')
 run([str(exe),'--out',str(root/'result')],'benchmark',90)
 idle('benchmark-idle-after')
 state['binary_after']=sha(exe);save()
 if state['binary_after']!=state['binary_before']:raise RuntimeError('binary changed')
 run([sys.executable,'-E',str(root/'check_results.py'),str(root/'result')],'analysis',60)
 if json.loads((root/'analysis.log').read_text()).get('pass') is not True:raise RuntimeError('analysis pass missing')
 state['state']='finished'
except Exception as e:state.update(state='failed',error=str(e))
finally:state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2));sys.exit(0 if state['state']=='finished' else 1)
