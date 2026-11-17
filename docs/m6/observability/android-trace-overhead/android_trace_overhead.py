from pathlib import Path
import subprocess,json,time
r=Path('<linux-home>/dev/framelink-m6');b=Path('<linux-home>/dev/build/framelink-m6-arm64');out=Path('<linux-home>/dev/evidence/m6-observability/android-trace-overhead');out.mkdir(exist_ok=True)
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];remote='/data/local/tmp/framelink-m6-20261106';root=remote+'/boundary-join-'+str(int(time.time()))
s={'state':'running','revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'scope':'three fixed off/on pairs, native GPU + testGDP; no universal overhead budget','started':time.time(),'steps':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,label):
 with (out/(label+'.log')).open('w') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 s['steps'].append({'name':label,'exit':p.returncode});save();assert p.returncode==0,label
save()
try:
 run(['cmake','--build',str(b),'--target','fl_native_boundary_benchmark','-j4'],'build')
 run(adb+['push',str(b/'fl_native_boundary_benchmark'),remote+'/fl_native_boundary_benchmark'],'push')
 for case in ['off-1','on-1','off-2','on-2','off-3','on-3']:
  hops=1
  d=root+'/'+case;run(adb+['shell',f'mkdir -p {d}'],'mkdir-'+case)
  env=f'LD_LIBRARY_PATH={remote} TMPDIR={remote} FL_BENCH_GPU=1 FL_BENCH_FRAMES=300 FL_BENCH_WARMUP=60 FL_BENCH_FPS=60 FL_BENCH_HOPS={hops} FL_BENCH_WIDTH=1920 FL_BENCH_HEIGHT=1080 FL_BENCH_AUDIO=1 FL_BENCH_OUT={d}/capture FRAMELINK_TRACE={d}/trace FRAMELINK_TRACE_EVENTS=65536'
  if case.startswith('off'):env=env.replace(f'FRAMELINK_TRACE={d}/trace','FRAMELINK_TRACE=')
  run(adb+['shell',f'cd {d} && {env} {remote}/fl_native_boundary_benchmark'],f'capture-{case}')
  run(adb+['pull',d,str(out)],f'pull-{case}')
  local=out/case;files=list(map(str,local.glob('trace.*.csv')))
  if case.startswith('on'):run(['python3',str(r/'tools/check_boundary_trace.py'),*files,'--frames','300','--hops',str(hops),'--gpu'],f'join-{case}')
  run(['python3',str(r/'tools/check_boundary_av.py'),str(local/'capture'),'--frames','300','--fps','60','--hops',str(hops)],f'av-{case}')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
