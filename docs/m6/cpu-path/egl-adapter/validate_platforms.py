from pathlib import Path
import subprocess,json,time,hashlib,base64,shlex
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-20261106'
s={'state':'running','steps':[],'source_sha256':hashlib.sha256((repo/'gst/gstframelink.cpp').read_bytes()).hexdigest()}
def save():(root/'platform-status.json').write_text(json.dumps(s,indent=2))
def run(cmd,name,timeout=300):
 with (root/(name+'.log')).open('w') as f:r=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 s['steps'].append({'name':name,'exit':r.returncode});save();assert r.returncode==0,name
 print(name+' passed',flush=True)
def ps(code,name):
 run(['ssh','-o','BatchMode=yes','-o','ServerAliveInterval=30','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],name)
save()
try:
 prefix=f"$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '{remote}'\n"
 ps(prefix+"if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'fl_*' -or $_.ProcessName -eq 'cmake' }).Count -ne 0) {throw 'Existing project executor'}",'windows-preflight')
 before=root/'gstframelink.windows-before.cpp'
 subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/gst/gstframelink.cpp',str(before)],check=True)
 assert before.read_bytes().replace(b'\r\n',b'\n')==subprocess.check_output(['git','show','HEAD:gst/gstframelink.cpp'],cwd=repo).replace(b'\r\n',b'\n'),'remote source drift'
 subprocess.run(['scp','-q',str(repo/'gst/gstframelink.cpp'),'<win-user>@<win-host>:'+remote+'/gst/gstframelink.cpp'],check=True)
 ps(prefix+'cmake --build build --config Release --target gstframelink -j 4\nexit $LASTEXITCODE','windows-build')
 ps(prefix+"ctest --test-dir build -C Release -R '^(gst_native_source_win|gst_native_source_win_copy_negative|gst_native_producer_win|gst_nv12_roundtrip|gst_rgb10a2_roundtrip|gst_pool_policy|gst_timestamp_contract|gst_policy_generation|gst_av_src_bin)$' --output-on-failure\nexit $LASTEXITCODE",'windows-focused')
 # The controller runs Android only after Windows has completed.
 adb=['adb','-s','A06B4AABFB07983'];live=subprocess.check_output(adb+['shell','ps -A'],text=True)
 assert not any(' fl_' in x for x in live.splitlines()),'Android executor already live'
 selected={'gst_pool_policy','gst_policy_generation','gst_av_src_bin','gst_av_qualification'}
 build='<linux-home>/dev/build/framelink-m6-arm64';device='/data/local/tmp/framelink-m6-20261106'
 tests=json.loads(subprocess.check_output(['ctest','--test-dir',build,'--show-only=json-v1']))['tests'];tests=[t for t in tests if t['name'] in selected];assert {t['name'] for t in tests}==selected
 targets=sorted({Path(t['command'][0]).name for t in tests}|{'fl_gst_nv12'})
 for abi in ['arm64','x86_64']:run(['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'--target']+targets+['-j4'],'android-'+abi+'-build')
 for target in targets:subprocess.run(adb+['push',build+'/'+target,device+'/'+target],check=True,stdout=subprocess.DEVNULL)
 for t in tests:
  cmd=[x.replace(build,device) if not x.endswith('.a') else 'static' for x in t['command']]
  run(adb+['shell','TMPDIR='+device+' LD_LIBRARY_PATH='+device+' '+shlex.join(cmd)],'android-'+t['name'])
 for fmt in ['nv12','rgb10a2']:
  cmd=[device+'/fl_gst_nv12']+(['rgb10a2'] if fmt=='rgb10a2' else [])
  run(adb+['shell','TMPDIR='+device+' LD_LIBRARY_PATH='+device+' '+shlex.join(cmd)],'android-'+fmt)
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s),flush=True);raise SystemExit(s['state']!='finished')
