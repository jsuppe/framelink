from pathlib import Path
import subprocess,json
root=Path(__file__).parent
results=[]
def run(name,cmd):
 with (root/(name+'.log')).open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 results.append({'name':name,'command':cmd,'exit':r.returncode});(root/'android-results.json').write_text(json.dumps(results,indent=2));assert r.returncode==0,name
run('linux-build',['cmake','--build','<linux-home>/dev/build/framelink-m6-linux','--target','gstframelink','fl_av_rendezvous','fl_gst_session_transport','fl_gst_local_av','fl_gst_shared_cpu_av','-j','4'])
run('linux-tests',['ctest','--test-dir','<linux-home>/dev/build/framelink-m6-linux','--output-on-failure','-R','^(av_rendezvous|gst_session_transport|gst_local_av|gst_shared_cpu_av)$'])
p=subprocess.run(['adb','shell','ps -A'],capture_output=True,text=True,timeout=30)
(root/'android-preflight.log').write_text(p.stdout+p.stderr);assert p.returncode==0
assert not any('fl_' in line for line in p.stdout.splitlines()),'Existing Android executor'
targets=['fl_av_rendezvous','fl_gst_session_transport','fl_gst_shared_cpu_av']
for abi in ['arm64','x86_64']:run(abi+'-build',['cmake','--build','<linux-home>/dev/build/framelink-m6-'+abi,'--target',*targets,'-j','4'])
remote='/data/local/tmp/framelink-local-native-regression'
run('mkdir',['adb','shell','mkdir','-p',remote])
for target in targets:run('push-'+target,['adb','push','<linux-home>/dev/build/framelink-m6-arm64/'+target,remote+'/'])
run('chmod',['adb','shell','chmod 755 '+remote+'/fl_*'])
for target in targets:run(target,['adb','shell','cd '+remote+' && ./'+target+' static'])
print('Android 3/3 shared regressions, both ABI builds passed')
