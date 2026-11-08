from pathlib import Path
import subprocess,json,time,hashlib,base64
root=Path('<linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/windows');root.mkdir(exist_ok=True)
repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-20261106'
files=['test/fl_gst_av_bench.cpp','test/fl_gst_av_harness.h']
s={'state':'running','started':time.time(),'source_sha256':{p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in files},'steps':[]}
def save():(root/'status.json').write_text(json.dumps(s,indent=2))
def ps(code,name,timeout=600):
 with (root/(name+'.log')).open('w') as f:r=subprocess.run(['ssh','-o','BatchMode=yes','-o','ServerAliveInterval=30','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 s['steps'].append({'name':name,'exit':r.returncode});save();assert r.returncode==0,name+' failed'
prefix=f"$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '{remote}'\n"
save()
try:
 ps(prefix+"if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -eq 'cmake'}).Count -ne 0) { throw 'Existing test process' }",'preflight')
 for p in files:
  old=root/(Path(p).name+'.before');subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/'+p,str(old)],check=True)
  prior=old.read_bytes().replace(b'\r\n',b'\n')
  revisions=subprocess.check_output(['git','log','--format=%H','--',p],cwd=repo,text=True).splitlines()
  comment=b'// Diagnostic fault: --video-stall-stage source|post-push\n//   --video-stall-frame N (1-based) --video-stall-ms N (1..1000).\n// CPU source-thread delay only; not GPU injection or qualification.\n'
  reviewed_comment_only=(p=='test/fl_gst_av_bench.cpp' and prior==subprocess.check_output(['git','show','HEAD:'+p],cwd=repo).replace(comment,b''))
  assert reviewed_comment_only or any(prior==subprocess.check_output(['git','show',revision+':'+p],cwd=repo).replace(b'\r\n',b'\n') for revision in revisions),'unrecognized remote drift '+p
 for p in files:subprocess.run(['scp','-q',str(repo/p),'<win-user>@<win-host>:'+remote+'/'+p],check=True)
 ps(prefix+'cmake --build build --config Release --target fl_gst_av_bench fl_gst_av_bin -j 4\nexit $LASTEXITCODE','build')
 ps(prefix+"ctest --test-dir build -C Release -R '^(gst_av_bench_smoke|gst_av_bin)$' --output-on-failure\nexit $LASTEXITCODE",'tests')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='finished' else 1)
