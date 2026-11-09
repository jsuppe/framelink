from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-34a391d'
def ps(code,name,timeout=900):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode

result=ps("Set-Location '"+remote+"'\n"+"""$env:GST_PLUGIN_PATH=(Get-Location).Path+'\\build\\Release'
$env:PATH=$env:GST_PLUGIN_PATH+';'+$env:PATH
$env:GST_REGISTRY=(Get-Location).Path+'\\api-registry.bin'
Get-FileHash build/Release/framelink.dll,build/Release/framelink_gst_session.dll,build/Release/gstframelink.dll -Algorithm SHA256 | Format-List
foreach($element in @('framelinksink','framelinksrc','framelinkavsink','framelinkavsrc')){gst-inspect-1.0 $element;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}
$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1
if(!$dump){throw 'dumpbin unavailable'}
& $dump.FullName /exports build/Release/framelink.dll
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $dump.FullName /exports build/Release/framelink_gst_session.dll
exit $LASTEXITCODE""",'api-runtime')
print('api exit',result)
assert result==0
import re
pattern=r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)'
old=set(re.findall(pattern,(repo/'docs/m6/windows-integrated-34a391d/api-runtime.log').read_text(),re.M))
new=set(re.findall(pattern,(root/'api-runtime.log').read_text(),re.M))
assert old and not old-new
assert 'fl_gst_session_open_local_native_epoch' in new-old
(root/'exports.json').write_text(json.dumps({'previous':len(old),'current':len(new),'removed':sorted(old-new),'added':sorted(new-old)},indent=2))
