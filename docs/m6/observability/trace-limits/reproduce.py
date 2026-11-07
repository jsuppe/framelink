from pathlib import Path
import subprocess,os,json,time,base64
r=Path('<linux-home>/dev/framelink-m6');out=Path('<linux-home>/dev/evidence/m6-observability/trace-limits');out.mkdir(exist_ok=True)
s={'state':'running','started':time.time(),'steps':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,label,env=None):
 with (out/(label+'.log')).open('w') as f:p=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 s['steps'].append({'name':label,'exit':p.returncode});save();assert p.returncode==0,label
save()
try:
 b='<linux-home>/dev/build/framelink-m6-linux'
 run(['cmake','--build',b,'--target','fl_trace_test','fl_trace_compiled_out','-j4'],'linux-build')
 run(['ctest','--test-dir',b,'-R','^trace_','--output-on-failure'],'linux-tests')
 p=out/'layout.cpp';p.write_text('#include "framelink_trace.hpp"\n#include <cstdio>\nint main(){printf("event_bytes=%zu default_per_thread_bytes=%zu max_threads_default_bytes=%zu max_configured_bytes=%zu\\n",sizeof(fltrace::Event),sizeof(fltrace::Event)*65536,sizeof(fltrace::Event)*65536*64,sizeof(fltrace::Event)*1048576*64);}\n')
 run(['c++','-std=c++17','-I'+str(r/'include'),str(p),'-o',str(out/'layout')],'layout-build');run([str(out/'layout')],'layout')
 b='<linux-home>/dev/build/framelink-m6-arm64';remote='/data/local/tmp/framelink-m6-20261106';adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983']
 run(['cmake','--build',b,'--target','fl_trace_test','fl_trace_compiled_out','-j4'],'android-build')
 for exe in ['fl_trace_test','fl_trace_compiled_out']:run(adb+['push',b+'/'+exe,remote+'/'+exe],'push-'+exe)
 for label,exe,args,on in [('disabled','fl_trace_test','',False),('concurrent','fl_trace_test','',True),('threads','fl_trace_test','threads',True),('details','fl_trace_test','details',True),('compiled-out','fl_trace_compiled_out','',True)]:
  prefix=remote+'/m6-trace-'+label if on else ''
  run(adb+['shell',f'LD_LIBRARY_PATH={remote} FRAMELINK_TRACE={prefix} FRAMELINK_TRACE_EVENTS=16 {remote}/{exe} {args}'],'android-'+label)
 code="$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH; Set-Location <win-home>/dev/framelink-m6-20261106; cmake --build build --config Release --target fl_trace_test fl_trace_compiled_out -j 4; if ($LASTEXITCODE -ne 0) {exit $LASTEXITCODE}; ctest --test-dir build -C Release -R '^trace_' --output-on-failure; exit $LASTEXITCODE"
 run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],'windows-tests')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
