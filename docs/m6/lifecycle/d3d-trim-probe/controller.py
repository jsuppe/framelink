from pathlib import Path
import subprocess,base64,json,time,csv,io
src=Path('<linux-home>/.openclaw/workspace-evelyn/.tmp/openclaw-spikes/d3d-lifecycle');root=Path('<linux-home>/dev/evidence/m6-lifecycle/d3d-trim-probe');root.mkdir(exist_ok=True);remote='<win-home>/dev/m6-d3d-trim-probe'
s={'state':'running','started':time.time(),'purpose':'Trim driver caches before device destruction each cycle, no transport','cases':[]};(root/'status.json').write_text(json.dumps(s,indent=2))
def ps(code,name,timeout=1000):
 with (root/name).open('w') as f:return subprocess.run(['ssh','-o','BatchMode=yes','-o','ServerAliveInterval=30','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
prefix=f"$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '{remote}'\n"
try:
 assert ps(f"New-Item -ItemType Directory -Force '{remote}' | Out-Null",'mkdir.log')==0
 for n in ['probe.cpp','CMakeLists.txt']:subprocess.run(['scp','-q',str(src/n),'<win-user>@<win-host>:'+remote+'/'+n],check=True)
 code=prefix+"if (@(Get-Process fl_gst_av_bench,d3d_lifecycle_probe -ErrorAction SilentlyContinue).Count -ne 0) { throw 'Another benchmark is running' }\ncmake -S . -B build -A x64\nif ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\ncmake --build build --config Release\nexit $LASTEXITCODE\n"
 s['build_exit']=ps(code,'build.log',300);assert s['build_exit']==0
 for mode,count in [('trim',2),('trim',100)]:
  label=f'{mode}-{count}'
  rc=ps(prefix+f"& ./build/Release/d3d_lifecycle_probe.exe {mode} {count} > {label}.csv\nexit $LASTEXITCODE\n",label+'.log')
  assert rc==0,label+' failed'
  subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/'+label+'.csv',str(root)],check=True)
  b=(root/(label+'.csv')).read_bytes();a=[{k:int(v) for k,v in x.items()} for x in csv.DictReader(io.StringIO(b.decode('utf-16' if b.startswith(b'\xff\xfe') else 'utf-8-sig')))];assert len(a)==count
  row={'label':label,'cycles':len(a),'max_device_release_refs':max(x['device_release_refs'] for x in a),'first':a[0],'last':a[-1]}
  if count>5:
   vals=a[5:];xm=sum(x['cycle'] for x in vals)/len(vals);ym=sum(x['private_bytes']/1024 for x in vals)/len(vals);row['postwarm_kb_per_cycle']=sum((x['cycle']-xm)*(x['private_bytes']/1024-ym) for x in vals)/sum((x['cycle']-xm)**2 for x in vals)
  s['cases'].append(row);(root/'status.json').write_text(json.dumps(s,indent=2));print(json.dumps(row),flush=True)
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();(root/'status.json').write_text(json.dumps(s,indent=2));print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='finished' else 1)
