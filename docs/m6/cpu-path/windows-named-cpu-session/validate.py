import subprocess,base64,pathlib,json,hashlib
root=pathlib.Path(__file__).parent
repo=pathlib.Path('<linux-home>/dev/framelink-m6')
remote='<win-home>/dev/framelink-m6-windows-30d7a53'
files=['src/fl_shared_cpu_endpoint_win.h','test/fl_gst_shared_cpu_session_win.cpp','gst/framelink_session_context.cpp','gst/framelink_session_transport.h']
(root/'source-sha256.json').write_text(json.dumps({x:hashlib.sha256((repo/x).read_bytes()).hexdigest() for x in files},indent=2))
for x in files:subprocess.run(['scp','-q',str(repo/x),'<win-user>@<win-host>:'+remote+'/'+x],check=True)
def ps(code,name):
 code="$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH;Set-Location '"+remote+"'\n"+code
 with (root/name).open('w') as f:
  r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=180)
 return r.returncode
result={}
result['build']=ps("cmake --build build --config Release --target fl_gst_shared_cpu_session_win -j 4\nexit $LASTEXITCODE",'build.log')
if result['build']==0:
 result['tests']=ps("ctest --test-dir build -C Release --output-on-failure -R '^(gst_shared_cpu_session_win|gst_shared_cpu_av|shared_cpu_pipe_win|shared_cpu_channel_win|gst_session_transport)$'\nexit $LASTEXITCODE",'tests.log')
(root/'results.json').write_text(json.dumps(result,indent=2));print(result)
