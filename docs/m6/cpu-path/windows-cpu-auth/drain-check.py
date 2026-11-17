from pathlib import Path
import subprocess,base64,json,hashlib
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-67c1523'
def ps(code,name):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+code
 with (root/(name+'.log')).open('w') as f:
  subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}\nWrite-Output 'No existing executor; remote is staged source tree, not git checkout'",'preflight')
files=['test/fl_gst_shared_cpu_session_win.cpp']
for f in files:subprocess.run(['scp','-q',str(repo/f),'<win-user>@<win-host>:'+remote+'/'+f],check=True)
ps('cmake -S . -B build\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake --build build --config Release --target fl_gst_shared_cpu_session_win\nexit $LASTEXITCODE','drain-build')
ps("ctest --test-dir build -C Release -R '^(gst_shared_cpu_session_win|gst_shared_cpu_av)$' --output-on-failure\nexit $LASTEXITCODE",'drain-tests')
(root/'drain-provenance.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
print((root/'drain-tests.log').read_text())

ps("Get-Content build/Testing/Temporary/LastTest.log",'drain-verbose')
ps("$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1\n& $dump.FullName /exports build/Release/framelink_gst_session.dll\nexit $LASTEXITCODE",'session-exports-new')
ps("$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1\n& $dump.FullName /exports <win-home>/dev/framelink-m6-windows-cf7408d/build/Release/framelink_gst_session.dll\nexit $LASTEXITCODE",'session-exports-old')
