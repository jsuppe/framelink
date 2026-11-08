from pathlib import Path
import subprocess,json,time,hashlib,base64,shlex
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-20261106'
files=['CMakeLists.txt','include/framelink_shared_cpu.h','src/framelink_shared_cpu.cpp','test/fl_shared_cpu_contract.cpp','test/fl_shared_cpu_c_api.c']
targets=['fl_shared_cpu_contract','fl_shared_cpu_c_api','fl_memory_contract','fl_memory_c_api','fl_memory_boundary','fl_session_contract','fl_session_c_api']
s={'state':'running','source_sha256':{p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in files},'steps':[]}
def save():(root/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,name,timeout=300):
 with (root/(name+'.log')).open('w') as f:r=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 s['steps'].append({'name':name,'exit':r.returncode});save();assert r.returncode==0,name;print(name+' passed',flush=True)
def ps(code,name):run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],name)
save()
try:
 prefix=f"$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '{remote}'\n"
 ps(prefix+"if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -eq 'cmake'}).Count -ne 0){throw 'Existing executor'}",'windows-preflight')
 old=root/'CMakeLists.windows-before.txt';subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/CMakeLists.txt',str(old)],check=True)
 # Preserve the remote platform-specific CMake baseline. Apply only the three
 # new contract additions; do not overwrite unrelated historical omissions.
 text=old.read_text();patches=[
 ('target_sources(framelink PRIVATE src/fl_trace.cpp src/framelink_session.cpp src/framelink_memory.cpp)', 'target_sources(framelink PRIVATE src/fl_trace.cpp src/framelink_session.cpp src/framelink_memory.cpp src/framelink_shared_cpu.cpp)'),
 ('PUBLIC_HEADER include/framelink_session.h include/framelink_memory.h)', 'PUBLIC_HEADER include/framelink_session.h include/framelink_memory.h include/framelink_shared_cpu.h)'),
 ('    add_executable(fl_memory_contract test/fl_memory_contract.cpp)', (repo/'CMakeLists.txt').read_text().split('    add_executable(fl_shared_cpu_contract test/fl_shared_cpu_contract.cpp)',1)[1].split('    add_executable(fl_memory_contract test/fl_memory_contract.cpp)',1)[0])]
 for before,after in patches[:2]:
  assert text.count(before)==1; text=text.replace(before,after)
 before,block=patches[2];assert text.count(before)==1;text=text.replace(before,'    add_executable(fl_shared_cpu_contract test/fl_shared_cpu_contract.cpp)'+block+before)
 patched=root/'CMakeLists.windows-patched.txt';patched.write_text(text)
 s['remote_cmake_baseline_sha256']=hashlib.sha256(old.read_bytes()).hexdigest();s['remote_cmake_tested_sha256']=hashlib.sha256(patched.read_bytes()).hexdigest();save()
 for f in files[1:]:
  ps(prefix+f"if(Test-Path '{f}'){{throw 'Unexpected preexisting new source: {f}'}}",'windows-new-'+Path(f).stem)
 for f in files:subprocess.run(['scp','-q',str(patched if f=='CMakeLists.txt' else repo/f),'<win-user>@<win-host>:'+remote+'/'+f],check=True)
 ps(prefix+'cmake --build build --config Release --target '+' '.join(targets)+' -j 4\nexit $LASTEXITCODE','windows-build')
 pattern='^(shared_cpu_contract|shared_cpu_contract_c_api|memory_contract|memory_contract_c_api|memory_boundary|session_contract|session_contract_c_api)$'
 ps(prefix+f"ctest --test-dir build -C Release -R '{pattern}' --output-on-failure\nexit $LASTEXITCODE",'windows-tests')
 adb=['adb','-s','A06B4AABFB07983'];live=subprocess.check_output(adb+['shell','ps -A'],text=True);assert not any(' fl_' in x for x in live.splitlines())
 for abi in ['arm64','x86_64']:run(['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'--target']+targets+['-j4'],'android-'+abi+'-build')
 device='/data/local/tmp/framelink-m6-20261106'
 for target in targets:
  subprocess.run(adb+['push','<linux-home>/dev/build/framelink-m6-arm64/'+target,device+'/'+target],check=True,stdout=subprocess.DEVNULL)
  run(adb+['shell','TMPDIR='+device+' LD_LIBRARY_PATH='+device+' '+device+'/'+target],'android-'+target)
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s),flush=True);raise SystemExit(s['state']!='finished')
