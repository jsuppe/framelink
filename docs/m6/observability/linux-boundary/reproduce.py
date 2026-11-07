from pathlib import Path
import subprocess,json,os,time
r=Path('<linux-home>/dev/framelink-m6');b=Path('<linux-home>/dev/build/framelink-m6-linux');out=Path('<linux-home>/dev/evidence/m6-observability/linux-boundary');out.mkdir(exist_ok=True)
s={'state':'running','revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'scope':'native boundary GPU completion + test-only GDP audio; not coordinated session/presentation qualification','started':time.time(),'steps':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,label,env=None):
 with (out/(label+'.log')).open('w') as f:p=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 s['steps'].append({'name':label,'exit':p.returncode});save();assert p.returncode==0,label
save()
try:
 run(['cmake','--build',str(b),'--target','fl_native_boundary_benchmark','-j4'],'build')
 for hops in [1,2]:
  d=out/str(hops);d.mkdir(exist_ok=True);prefix=str(d/'capture');env=os.environ.copy();env.update(FL_BENCH_GPU='1',FL_BENCH_FRAMES='300',FL_BENCH_WARMUP='60',FL_BENCH_FPS='60',FL_BENCH_HOPS=str(hops),FL_BENCH_WIDTH='1920',FL_BENCH_HEIGHT='1080',FL_BENCH_AUDIO='1',FL_BENCH_OUT=prefix,FRAMELINK_TRACE=str(d/'trace'),FRAMELINK_TRACE_EVENTS='65536')
  run([str(b/'fl_native_boundary_benchmark')],f'capture-{hops}',env)
  files=list(map(str,d.glob('trace.*.csv')))
  run(['python3',str(r/'tools/check_boundary_trace.py'),*files,'--frames','300','--hops',str(hops),'--gpu'],f'join-{hops}')
  run(['python3',str(r/'tools/check_boundary_av.py'),prefix,'--frames','300','--fps','60','--hops',str(hops)],f'av-{hops}')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
