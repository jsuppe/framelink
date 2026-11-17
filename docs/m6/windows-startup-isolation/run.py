from pathlib import Path
import subprocess,base64,json,datetime
root=Path(__file__).parent
remote='<win-home>/dev/framelink-m6-windows-548fe19'
status={'revision':'548fe19b8c11b84758653aafe0df812f92cbc9ab','state':'running','hypothesis':'legacy audio clock uses lazy first-render producer open; audio-coordinated imports before epoch','started':datetime.datetime.now(datetime.timezone.utc).isoformat()}
(root/'status.json').write_text(json.dumps(status,indent=2))
for mode in ['audio','audio-coordinated']:
 out=remote+'/startup-isolation-'+mode
 code=f"""$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PATH='{remote}/build/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
Set-Location '{remote}'
if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {{$_.ProcessName -like 'fl_*' -or $_.ProcessName -like 'framelink*' -or $_.ProcessName -match 'ctest|MSBuild'}}).Count -ne 0){{throw 'Existing executor'}}
New-Item -ItemType Directory -Path '{out}' -ErrorAction Stop | Out-Null
$env:FRAMELINK_TRACE='{out}/native.csv'
$env:FRAMELINK_GST_TRACE='{out}/gst.csv'
$env:GST_DEBUG='framelink:4'
python test/m5_external_pipeline.py --build build/Release --output '{out}' --clock-policy {mode}
exit $LASTEXITCODE
"""
 with (root/(mode+'.log')).open('w') as f:r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=150)
 status[mode+'_exit']=r.returncode
 pull=subprocess.run(['scp','-rq','<win-user>@<win-host>:'+out,str(root)],capture_output=True,text=True,timeout=60)
 status[mode+'_pull_exit']=pull.returncode
 (root/'status.json').write_text(json.dumps(status,indent=2))
status['state']='finished';status['ended']=datetime.datetime.now(datetime.timezone.utc).isoformat();(root/'status.json').write_text(json.dumps(status,indent=2));print(json.dumps(status))
