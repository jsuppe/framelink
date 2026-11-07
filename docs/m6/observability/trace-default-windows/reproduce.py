from pathlib import Path
import subprocess,json,time,base64
r=Path('<linux-home>/dev/framelink-m6');out=Path('<linux-home>/dev/evidence/m6-observability/trace-default-windows');out.mkdir(exist_ok=True);remote='<win-home>/dev/framelink-m6-20261106'
s={'state':'running','started':time.time(),'scope':'preserve remote platform-specific CMake omissions; add only three trace-capacity tests','steps':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
save()
try:
 prev=Path('<linux-home>/dev/evidence/m6-observability/trace-default-repair/CMakeLists.txt.before').read_bytes().replace(b'\r\n',b'\n')
 current=out/'CMakeLists.current';subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/CMakeLists.txt',str(current)],check=True);assert current.read_bytes().replace(b'\r\n',b'\n')==prev,'new remote change'
 local=(r/'CMakeLists.txt').read_text();begin=local.index('    add_test(NAME trace_default_capacity');end=local.index('    add_test(NAME trace_bounded_concurrent',begin);block=local[begin:end];old=prev.decode();assert old.count('    add_test(NAME trace_bounded_concurrent')==1
 patched=out/'CMakeLists.txt';patched.write_text(old.replace('    add_test(NAME trace_bounded_concurrent',block+'    add_test(NAME trace_bounded_concurrent'))
 subprocess.run(['scp','-q',str(patched),'<win-user>@<win-host>:'+remote+'/CMakeLists.txt'],check=True)
 for name in ['include/framelink_trace.hpp','test/fl_trace_test.cpp']:
  p=out/Path(name).name;subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/'+name,str(p)],check=True);assert p.read_bytes().replace(b'\r\n',b'\n')==(r/name).read_bytes().replace(b'\r\n',b'\n')
 code="$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH; Set-Location '"+remote+"'; cmake --build build --config Release --target fl_trace_test fl_trace_compiled_out -j 4; if ($LASTEXITCODE -ne 0) {exit $LASTEXITCODE}; ctest --test-dir build -C Release -R '^trace_' --output-on-failure; exit $LASTEXITCODE"
 with (out/'tests.log').open('w') as f:p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=180)
 s['steps'].append({'name':'windows-tests','exit':p.returncode});assert p.returncode==0
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
