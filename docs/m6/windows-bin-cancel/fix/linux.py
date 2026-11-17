import subprocess,json,datetime,os
from pathlib import Path
r=Path(__file__).parent
build='<linux-home>/dev/build/framelink-m6-event-linux-20261112'
env=dict(os.environ,PKG_CONFIG_PATH='<linux-home>/dev/build/framelink-host-sdk-20261112/usr/lib/x86_64-linux-gnu/pkgconfig')
s={'started':datetime.datetime.now(datetime.timezone.utc).isoformat()}
commands=[('build',['cmake','--build',build,'--target','fl_gst_event_channel','fl_gst_session_context','fl_gst_session_transport','fl_gst_av_bin','gstframelink','-j','4']),('tests',['ctest','--test-dir',build,'-R','^gst_(event_channel|session_context|session_transport|av_bin)$','--output-on-failure'])]
for name,cmd in commands:
 with (r/('linux-'+name+'.log')).open('w') as f:p=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 s[name]={'command':cmd,'exit':p.returncode};(r/'linux-status.json').write_text(json.dumps(s,indent=2));print(name,p.returncode,flush=True)
 if p.returncode:raise SystemExit(p.returncode)
s['ended']=datetime.datetime.now(datetime.timezone.utc).isoformat();(r/'linux-status.json').write_text(json.dumps(s,indent=2))
