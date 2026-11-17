import subprocess,base64,pathlib,json
root=pathlib.Path(__file__).parent
remote='<win-home>/dev/framelink-m6-windows-d60f683'
def ps(code,name):
 code="$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH;Set-Location '"+remote+"'\n"+code
 with (root/(name+'.log')).open('w') as f:
  r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=240)
 return r.returncode
r={}
r['build']=ps("cmake --build build --config Release --target fl_gst_av_bench -j 4\nexit $LASTEXITCODE",'build')
if r['build']==0:r['tests']=ps("ctest --test-dir build -C Release --output-on-failure -R '^(gst_av_bench_smoke|gst_av_bench_shared_cpu_.*|gst_shared_cpu_av)$'\nexit $LASTEXITCODE",'tests')
(root/'results.json').write_text(json.dumps(r,indent=2));print(r)
