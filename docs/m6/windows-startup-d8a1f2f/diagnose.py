import subprocess,base64,json,datetime
from pathlib import Path
root=Path(__file__).parent
remote='<win-home>/dev/framelink-m6-windows-d8a1f2f'
code="""$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
if (@(Get-Process | Where-Object {$_.ProcessName -match 'fl_|framelink|ctest|cmake|MSBuild'}).Count -ne 0){throw 'Existing executor'}
Set-Location '<win-home>/dev/framelink-m6-windows-d8a1f2f'
New-Item -ItemType Directory -Path startup-native-trace | Out-Null
$env:FRAMELINK_TRACE=(Get-Location).Path+'/startup-native-trace/transport'
$env:FRAMELINK_TRACE_EVENTS='65536'
python test/m5_preconnect.py --build build/Release --output startup-native-trace/preconnect
exit $LASTEXITCODE
"""
status={'state':'running','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'purpose':'one bounded native-trace preconnect control; original failures retained; no pass waiver'}
(root/'diagnostic-status.json').write_text(json.dumps(status,indent=2))
with (root/'diagnostic.log').open('w') as f:r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=180)
status['exit']=r.returncode
status['pull_exit']=subprocess.run(['scp','-qr','<win-user>@<win-host>:'+remote+'/startup-native-trace',str(root)]).returncode
status['state']='collected';status['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
(root/'diagnostic-status.json').write_text(json.dumps(status,indent=2));print(json.dumps(status))
