from pathlib import Path
import subprocess,base64,json,hashlib
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-20261106'
files=['src/fl_shared_cpu_pool.h','test/fl_shared_cpu_pool.cpp']
def ps(code,name):
 cmd=['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(("$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+code).encode('utf-16le')).decode()]
 with (root/(name+'.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -eq 'cmake'}).Count -ne 0){throw 'Existing executor'}",'preflight')
for f in files:ps(f"if(Test-Path '{f}'){{throw 'Unexpected existing source'}}",'absent-'+Path(f).stem)
old=root/'CMakeLists.windows-before.txt';subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/CMakeLists.txt',str(old)],check=True)
s=old.read_text();anchor='    add_executable(fl_shared_cpu_contract test/fl_shared_cpu_contract.cpp)';assert s.count(anchor)==1
block=(repo/'CMakeLists.txt').read_text().split('    add_executable(fl_shared_cpu_pool ',1)[1].split(anchor,1)[0]
new=root/'CMakeLists.windows-tested.txt';new.write_text(s.replace(anchor,'    add_executable(fl_shared_cpu_pool '+block+anchor))
for f in files:subprocess.run(['scp','-q',str(repo/f),'<win-user>@<win-host>:'+remote+'/'+f],check=True)
subprocess.run(['scp','-q',str(new),'<win-user>@<win-host>:'+remote+'/CMakeLists.txt'],check=True)
ps('cmake --build build --config Release --target fl_shared_cpu_pool\nexit $LASTEXITCODE','build')
ps("ctest --test-dir build -C Release -R '^(shared_cpu_pool|shared_cpu_contract|shared_cpu_contract_c_api|session_contract|memory_contract)$' --output-on-failure\nexit $LASTEXITCODE",'tests')
(root/'provenance.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
print((root/'tests.log').read_text())
