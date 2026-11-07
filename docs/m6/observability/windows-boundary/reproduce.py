from pathlib import Path
import subprocess,json,time,hashlib
r=Path('<linux-home>/dev/framelink-m6');out=Path('<linux-home>/dev/evidence/m6-observability/windows-boundary');out.mkdir(exist_ok=True);remote='<win-home>/dev/framelink-m6-20261106';tag='m6-boundary-'+str(int(time.time()))
s={'state':'running','scope':'native D3D11 copy/readback completion + test GDP PCM; not coordinated session/presentation','started':time.time(),'steps':[],'source_sha256':{}}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,label):
 with (out/(label+'.log')).open('w') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=300)
 s['steps'].append({'name':label,'exit':p.returncode});save();assert p.returncode==0,label
def ps(body,label):
 p=out/(label+'.ps1');p.write_text("$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+body)
 subprocess.run(['scp','-q',str(p),'<win-user>@<win-host>:'+remote+'/'+tag+'.ps1'],check=True)
 run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-File',remote+'/'+tag+'.ps1'],label)
save()
try:
 for name in ['test/fl_native_boundary_benchmark_win.cpp','test/fl_boundary_audio.hpp','test/fl_boundary_resources.hpp','include/framelink_trace.hpp','samples/native_relay.h']:
  tmp=out/(Path(name).name+'.remote');subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/'+name,str(tmp)],check=True)
  assert tmp.read_bytes().replace(b'\r\n',b'\n')==(r/name).read_bytes().replace(b'\r\n',b'\n'),'source drift '+name
  s['source_sha256'][name]=hashlib.sha256(tmp.read_bytes()).hexdigest();save()
 ps('cmake --build build --config Release --target fl_native_boundary_benchmark_win -j 4\nexit $LASTEXITCODE','build')
 for hops in [1,2]:
  dest=remote+'/'+tag+'/'+str(hops)
  env={'FL_BENCH_FRAMES':'300','FL_BENCH_WARMUP':'60','FL_BENCH_FPS':'60','FL_BENCH_HOPS':str(hops),'FL_BENCH_WIDTH':'1920','FL_BENCH_HEIGHT':'1080','FL_BENCH_AUDIO':'1','FL_BENCH_FORMAT':'BGRA','FL_BENCH_OUT':dest+'/capture','FRAMELINK_TRACE':dest+'/trace','FRAMELINK_TRACE_EVENTS':'65536','FL_BENCH_TRACE':dest+'/completion','FL_BENCH_TRACE_EVENTS':'65536'}
  ps("New-Item -ItemType Directory -Force '"+dest+"' | Out-Null\n"+'\n'.join("$env:"+k+"='"+v+"'" for k,v in env.items())+'\n./build/Release/fl_native_boundary_benchmark_win.exe\nexit $LASTEXITCODE',f'capture-{hops}')
  run(['scp','-q','-r','<win-user>@<win-host>:'+dest,str(out)],f'pull-{hops}');d=out/str(hops)
  run(['python3',str(r/'tools/check_boundary_trace.py'),*map(str,d.glob('trace.*.csv')),'--frames','300','--hops',str(hops),'--gpu','--producer-map-operation','flImageMapPlanes','--private-readback-bytes',str(1920*1080*4),'--completion-files',*map(str,d.glob('completion.*.csv'))],f'join-{hops}')
  run(['python3',str(r/'tools/check_boundary_av.py'),str(d/'capture'),'--frames','300','--fps','60','--hops',str(hops)],f'av-{hops}')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
