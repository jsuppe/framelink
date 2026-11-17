from pathlib import Path
import os,json,hashlib,subprocess,sys,datetime
root=Path(__file__).resolve().parent;spec=json.loads((root/'config.json').read_text());src=root/'source';build=root/'build';out=root/'results';out.mkdir()
state=dict(state='preparing',revision=spec['revision'],started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
def save():(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(args,name,timeout=120,cwd=root,required=True):
 state.update(step=name,command=args);save()
 with (out/(name+'.log')).open('w') as f:
  p=subprocess.Popen(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=f,stderr=subprocess.STDOUT,timeout=30);p.wait(timeout=30);raise RuntimeError(name+' timeout; owned tree only')
 state.pop('owned_pid',None);state[name+'_exit']=rc;save()
 if rc and required:raise RuntimeError(name+' failed; next arm withheld')
 return rc
def sourcepins():return {p.relative_to(src).as_posix():sha(p) for p in src.rglob('*') if p.is_file()}
def pins():return {n:sha(build/'Release'/n) for n in ('fl_gst_local_native_bench_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll')}
def idle(label):
 code="$p=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$'}); @{utc=[DateTime]::UtcNow.ToString('o'); activeRuntime=@($p|Select-Object ProcessId,Name)}|ConvertTo-Json -Depth 4; if($p.Count){exit 1}"
 run(['powershell','-NoProfile','-Command',code],label,30)
save()
try:
 for k in list(os.environ):
  if k.upper().startswith(('FRAMELINK_','FL_BENCH_','FL_FAULT','GST_')):os.environ.pop(k,None)
 os.environ.update(MSBUILDDISABLENODEREUSE='1',GST_REGISTRY=str(root/'registry.bin'));os.environ['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+os.environ['PATH']
 assert sha(root/'source.tar')==spec['archive_sha256']
 src.mkdir();run(['tar','-xf',str(root/'source.tar'),'-C',str(src)],'extract');assert sourcepins()==spec['source_pins']
 run(['git','-c','core.autocrlf=false','-c','core.eol=lf','apply','--check',str(root/'diagnostic.patch')],'patch-check',cwd=src)
 run(['git','-c','core.autocrlf=false','-c','core.eol=lf','apply',str(root/'diagnostic.patch')],'patch-apply',cwd=src)
 expected=dict(spec['source_pins']);expected['test/fl_gst_local_native_bench_win.cpp']=spec['diagnostic_sha256'];assert sourcepins()==expected
 state['overlay_source_sha256']=spec['diagnostic_sha256'];save()
 run(['cmake','-S',str(src),'-B',str(build),'-G','Visual Studio 17 2022','-DFRAMELINK_BUILD_TESTS=ON','-DFRAMELINK_BUILD_GST=ON','-DFRAMELINK_BUILD_SAMPLES=ON'],'configure',240)
 run(['cmake','--build',str(build),'--config','Release','--target','fl_gst_local_native_bench_win','gstframelink','-j','4','--','/nr:false'],'build',1800)
 state['binary_before']=pins();save();bins=build/'Release';os.environ['PATH']=str(bins)+';'+os.environ['PATH']
 common=[str(bins/'fl_gst_local_native_bench_win.exe'),str(bins/'gstframelink.dll'),'--seconds','5','--width','1920','--height','1080','--fps','60','--format','BGRA']
 for name,trace,pause in [('control-off',False,False),('control-on',True,False),('injected-on',True,True)]:
  idle(name+'-idle-before');state[name+'_binary_before']=pins();assert pins()==state['binary_before'];save()
  args=common+['--out',str(out/name)]+(['--trace'] if trace else [])+(['--diagnostic-source-pause-ms','350'] if pause else [])
  rc=run(args,name,90,required=False);state[name+'_binary_after']=pins();save();assert pins()==state['binary_before'];idle(name+'-idle-after')
  gate=run([sys.executable,'-E',str(root/'check_local_native_bench.py'),str(out/name)],name+'-qualification-analysis',60,required=False)
  run([sys.executable,'-E',str(root/'analyze_diagnostic.py'),str(out/name),'injected' if pause else 'control'],name+'-phase-analysis',60)
  if not pause and (rc or gate):raise RuntimeError(name+' baseline failed; no injected arm')
  if pause and not (rc or gate):raise RuntimeError('injected delay did not fail original timing/identity gate')
 state['state']='diagnostic-complete';state['qualification_pass']=False
except Exception as e:state.update(state='failed',error=str(e))
finally:state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2));sys.exit(0 if state['state']=='diagnostic-complete' else 1)
