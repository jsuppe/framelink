import pathlib,subprocess,json,hashlib,os
root=pathlib.Path(__file__).parent
results=[]
def run(label,cmd,env=None):
 with (root/(label+'.log')).open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=240)
 results.append({'step':label,'command':cmd,'exit':r.returncode});(root/'final-results.json').write_text(json.dumps(results,indent=2));print(label,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
for abi in ['linux','arm64','x86_64']:
 run('final-build-'+abi,['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'-j4','--target','fl_gst_shared_cpu_source'])
 if abi=='linux':
  env=dict(os.environ,FRAMELINK_GST_TRACE=str(root/'source-trace.json'))
  run('final-linux-source',['<linux-home>/dev/build/framelink-m6-linux/fl_gst_shared_cpu_source','<linux-home>/dev/build/framelink-m6-linux/libgstframelink.so'],env)
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];remote='/data/local/tmp/framelink-cpu-gst-source'
run('final-push',adb+['push','<linux-home>/dev/build/framelink-m6-arm64/fl_gst_shared_cpu_source',remote+'/'])
run('final-android-source',adb+['shell','cd '+remote+' && ./fl_gst_shared_cpu_source static'])
repo=pathlib.Path('<linux-home>/dev/framelink-m6');files=['CMakeLists.txt','include/framelink_shared_cpu_channel.h','src/framelink_shared_cpu_channel.cpp','gst/gstframelink.cpp','gst/fl_gst_shared_cpu_source.inc','test/fl_gst_shared_cpu_source.cpp']
(root/'source-sha256.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
