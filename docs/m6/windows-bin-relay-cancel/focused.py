from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-7848cb6'
def ps(code,name,timeout=900):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode


result=ps("Set-Location '"+remote+"'\nctest --test-dir build -C Release --output-on-failure -R '^(gst_native_bin_relay_cancel_win|gst_native_bin_relay_win|gst_native_lease_relay_win|gst_automatic_relay_negative_win|gst_native_bin_cancel_win|gst_native_relay_av_win|gst_native_relay_process_win|gst_native_relay_downstream_death_win|gst_native_producer_win|gst_av_bin)$'\nexit $LASTEXITCODE",'focused')
assert result==0
result=ps("Set-Location '"+remote+"'\n& ./build/Release/fl_gst_native_producer_win.exe ./build/Release/gstframelink.dll --bin-relay-cancel-diagnostic\nexit $LASTEXITCODE",'diagnostic')
assert result==0
(root/'focused-status.json').write_text(json.dumps({'focused_exit':0,'diagnostic_exit':result,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2))
print('focused/diagnostic exit0')
