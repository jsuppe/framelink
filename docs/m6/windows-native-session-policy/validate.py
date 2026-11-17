import subprocess,base64,pathlib,json,hashlib
root=pathlib.Path(__file__).parent
repo=pathlib.Path('<linux-home>/dev/framelink-m6')
remote='<win-home>/dev/framelink-m6-windows-d60f683'
files=['gst/gstframelink.cpp','test/fl_gst_native_producer_win.cpp','CMakeLists.txt']
(root/'source-sha256.json').write_text(json.dumps({x:hashlib.sha256((repo/x).read_bytes()).hexdigest() for x in files},indent=2))
for x in files:subprocess.run(['scp','-q',str(repo/x),'<win-user>@<win-host>:'+remote+'/'+x],check=True)
def ps(code,name):
 code="$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH;Set-Location '"+remote+"'\n"+code
 with (root/name).open('w') as f:
  r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=180)
 return r.returncode
result={}
result['build']=ps("cmake --build build --config Release --target fl_gst_native_producer_win gstframelink -j 4\nexit $LASTEXITCODE",'build.log')
if result['build']==0:
 result['tests']=ps("ctest --test-dir build -C Release --output-on-failure -R '^(gst_native_producer_win|gst_native_source_win|gst_session_context|gst_session_transport|gst_shared_cpu_av)$'\nexit $LASTEXITCODE",'tests.log')
(root/'results.json').write_text(json.dumps(result,indent=2));print(result)
