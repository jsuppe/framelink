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
targets=['fl_gst_session_transport','fl_gst_shared_cpu_av']
for abi in ['linux','arm64','x86_64']:
 run('build-'+abi,['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'-j4','--target']+targets)
 if abi=='linux':run('linux',['ctest','--test-dir','<linux-home>/dev/build/framelink-m6-linux','-R','^(gst_session_transport|gst_shared_cpu_av|av_auth)$','--output-on-failure'])
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983']
remote='/data/local/tmp/framelink-cpu-bootstrap'
run('device-process',adb+['shell','ps -A -o PID,ARGS'])
run('mkdir',adb+['shell','mkdir -p '+remote])
for t in targets:run('push-'+t,adb+['push','<linux-home>/dev/build/framelink-m6-arm64/'+t,remote+'/'])
run('chmod',adb+['shell','chmod 755 '+remote+'/fl_*'])
for t in targets:run('android-'+t,adb+['shell','cd '+remote+' && ./'+t+' static'])
files=['test/fl_gst_shared_cpu_av.cpp','runtime/av_auth.cpp','gst/framelink_session_transport.cpp','gst/framelink_session_transport.h','test/fl_gst_session_transport.cpp']
(root/'source-sha256.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
