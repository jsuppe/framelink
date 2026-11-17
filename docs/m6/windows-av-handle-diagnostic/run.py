from pathlib import Path
import subprocess,base64,json,datetime
root=Path(__file__).parent
status={'revision':'3ca89e54e64d7a0baf8ba52e4c12ee0cdfd9c6f0','mode':'temporary test-only bin CPU 100-cycle handle-type diagnostic','state':'starting'}
def record():
 status['utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();(root/'status.json').write_text(json.dumps(status,indent=2))
def ps(code,name,timeout):
 code="$ErrorActionPreference='Stop'\n$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH\nSet-Location '<win-home>/dev/framelink-m6-windows-3ca89e5'\n"+code
 with (root/(name+'.log')).open('w') as f:
  return subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
record()
try:
 status['build_exit']=ps('cmake --build build --config Release --target fl_gst_av_qualification -j 4\nexit $LASTEXITCODE','build',180);record()
 assert status['build_exit']==0
 status['state']='testing';record()
 status['test_exit']=ps('& ./build/Release/fl_gst_av_qualification.exe ./build/Release/gstframelink.dll\nexit $LASTEXITCODE','diagnostic',600)
 status['state']='finished';record()
except Exception as e:
 status['state']='failed';status['error']=str(e);record();raise
