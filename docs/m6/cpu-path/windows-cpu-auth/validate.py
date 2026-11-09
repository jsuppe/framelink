from pathlib import Path
import subprocess,base64,json,hashlib
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-67c1523'
def ps(code,name):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+code
 with (root/(name+'.log')).open('w') as f:
  subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}\nWrite-Output 'No existing executor; remote is staged source tree, not git checkout'",'preflight')
files=['gst/framelink_session_context.cpp','gst/framelink_session_transport.h','gst/fl_gst_cpu_pipe_auth_win.h','src/fl_shared_cpu_pipe_win.h','src/fl_protocol.h','test/fl_gst_shared_cpu_session_win.cpp','test/fl_gst_shared_cpu_av.cpp']
for f in files:subprocess.run(['scp','-q',str(repo/f),'<win-user>@<win-host>:'+remote+'/'+f],check=True)
ps('cmake -S . -B build\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake --build build --config Release --target gstframelink fl_gst_shared_cpu_session_win fl_gst_shared_cpu_av fl_gst_session_transport fl_shared_cpu_pipe_win\nexit $LASTEXITCODE','build')
ps("ctest --test-dir build -C Release -R '^(gst_shared_cpu_av|gst_shared_cpu_source|gst_shared_cpu_roundtrip|gst_shared_cpu_session_win|gst_session_transport|shared_cpu_pipe_win)$' --output-on-failure\nexit $LASTEXITCODE",'final-tests')
(root/'final-provenance.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
print((root/'final-tests.log').read_text())
