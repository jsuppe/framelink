import subprocess,json,datetime,shlex
from pathlib import Path
r=Path(__file__).parent;s={'started':datetime.datetime.now(datetime.timezone.utc).isoformat()};adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];remote='/data/local/tmp/framelink-drain-20261112'
def run(name,cmd,timeout=180):
 with (r/(name+'.log')).open('w') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 s[name]=p.returncode;(r/'android-status.json').write_text(json.dumps(s,indent=2));print(name,p.returncode,flush=True)
 if p.returncode:raise SystemExit(p.returncode)
run('android-inventory',adb+['shell','ps -A | grep -E "fl_|framelink" || true'])
for abi in ['arm64','x86_64']:
 run(abi+'-build',['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'--target','fl_gst_event_channel','fl_gst_session_context','fl_gst_session_transport','-j','4'])
run('android-dir',adb+['shell','mkdir -p '+remote])
for name in ['fl_gst_event_channel','fl_gst_session_context','fl_gst_session_transport']:
 run(name+'-push',adb+['push','<linux-home>/dev/build/framelink-m6-arm64/'+name,remote+'/'])
run('android-libcxx',adb+['push','<linux-home>/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so',remote+'/'])
for name in ['fl_gst_event_channel','fl_gst_session_context','fl_gst_session_transport']:
 run(name,adb+['shell','cd '+remote+' && chmod +x '+name+' && TMPDIR='+remote+' LD_LIBRARY_PATH='+remote+' ./'+name],40)
s['ended']=datetime.datetime.now(datetime.timezone.utc).isoformat();(r/'android-status.json').write_text(json.dumps(s,indent=2))
