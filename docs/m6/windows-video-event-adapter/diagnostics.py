from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-abba3eb'
def ps(code,name,timeout=900):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode

status={}
for mode in ['video-relay-event-controls','bin-relay-events-diagnostic','bin-relay-cancel-diagnostic']:
 result=ps("Set-Location '"+remote+"'\n& ./build/Release/fl_gst_native_producer_win.exe ./build/Release/gstframelink.dll --"+mode+"\nexit $LASTEXITCODE",mode)
 status[mode]=result
 assert result==0
(root/'diagnostic-status.json').write_text(json.dumps(status,indent=2))
