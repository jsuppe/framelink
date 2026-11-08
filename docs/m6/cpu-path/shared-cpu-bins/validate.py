import pathlib,subprocess,json,hashlib
root=pathlib.Path(__file__).parent/'final';root.mkdir(exist_ok=True)
repo=pathlib.Path('<linux-home>/dev/framelink-m6')
results=[]
def run(label,cmd):
 with (root/(label+'.log')).open('w') as out:
  r=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT,timeout=240)
 results.append({'step':label,'command':cmd,'exit':r.returncode});(root/'results.json').write_text(json.dumps(results,indent=2))
 print(label,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
targets=['fl_gst_shared_cpu_av','fl_gst_session_context','fl_gst_shared_cpu_events','fl_gst_shared_cpu_roundtrip','fl_gst_audio_source','fl_gst_av_bin']
for abi in ['linux','arm64','x86_64']:
 run('build-'+abi,['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'-j4','--target']+targets)
 if abi=='linux':run('linux',['ctest','--test-dir','<linux-home>/dev/build/framelink-m6-linux','-R','^(gst_shared_cpu_av|gst_session_context|gst_shared_cpu_events|gst_shared_cpu_roundtrip|gst_audio_source|gst_av_bin)$','--output-on-failure'])
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983']
remote='/data/local/tmp/framelink-cpu-bins'
run('device-process',adb+['shell','ps -A -o PID,ARGS'])
run('mkdir',adb+['shell','mkdir -p '+remote])
for t in targets:run('push-'+t,adb+['push','<linux-home>/dev/build/framelink-m6-arm64/'+t,remote+'/'])
run('chmod',adb+['shell','chmod 755 '+remote+'/fl_*'])
for t in targets:run('android-'+t,adb+['shell','cd '+remote+' && ./'+t+' static'])
files=['gst/gstframelinkavsrc.cpp','gst/gstframelinkavsink.cpp','test/fl_gst_shared_cpu_av.cpp','test/fl_gst_session_context.cpp','gst/framelink_session_context.cpp','CMakeLists.txt','include/framelink_shared_cpu_channel.h','src/fl_shared_cpu_control.h','src/framelink_shared_cpu_channel.cpp','gst/gstframelink.cpp','gst/fl_gst_shared_cpu_source.inc','gst/fl_gst_shared_cpu_sink.inc','test/fl_gst_shared_cpu_events.cpp','test/fl_gst_shared_cpu_roundtrip.cpp']
(root/'source-sha256.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
