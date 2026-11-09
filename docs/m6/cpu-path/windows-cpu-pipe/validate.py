from pathlib import Path
import subprocess,base64,json,hashlib
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-609cb74'
def ps(code,name):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+code
 with (root/(name+'.log')).open('w') as f:
  subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}\nWrite-Output 'No existing executor; remote is staged source tree, not git checkout'",'preflight')
old=root/'CMakeLists.before.txt'
if not old.exists():subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/CMakeLists.txt',str(old)],check=True)
s=old.read_text();anchor='    add_executable(fl_shared_cpu_pool test/fl_shared_cpu_pool.cpp)';assert s.count(anchor)==1
block="""        add_executable(fl_shared_cpu_pipe_win test/fl_shared_cpu_pipe_win.cpp)
        target_include_directories(fl_shared_cpu_pipe_win PRIVATE include src)
        add_test(NAME shared_cpu_pipe_win COMMAND fl_shared_cpu_pipe_win)
        set_tests_properties(shared_cpu_pipe_win PROPERTIES TIMEOUT 20)
"""
new=root/'CMakeLists.tested.txt';new.write_text(s.replace('        add_executable(fl_shared_cpu_imported_pool_win',block+'        add_executable(fl_shared_cpu_imported_pool_win'))
files=['src/fl_shared_cpu_pipe_win.h','test/fl_shared_cpu_pipe_win.cpp']
for f in files:subprocess.run(['scp','-q',str(repo/f),'<win-user>@<win-host>:'+remote+'/'+f],check=True)
subprocess.run(['scp','-q',str(new),'<win-user>@<win-host>:'+remote+'/CMakeLists.txt'],check=True)
ps('cmake --build build --config Release --target fl_shared_cpu_pipe_win\nexit $LASTEXITCODE','build')
ps("ctest --test-dir build -C Release -R '^(shared_cpu_pipe_win|shared_cpu_imported_pool_win|shared_cpu_allocation_win|ipc_framing_win)$' --output-on-failure\nexit $LASTEXITCODE",'tests')
(root/'provenance.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
print((root/'tests.log').read_text())
