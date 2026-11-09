from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-02509f7'
def ps(code,name,timeout=900):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode
status={'revision':subprocess.check_output(['git','-C',str(repo),'rev-parse','02509f7'],text=True).strip(),'state':'starting','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'remote':remote}
def record(): (root/'status.json').write_text(json.dumps(status,indent=2))
record()
try:
 code="if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}\nif(Test-Path '"+remote+"'){throw 'Existing destination'}\nNew-Item -ItemType Directory -Path '"+remote+"' | Out-Null"
 assert ps(code,'preflight')==0
 archive=root/'source.tar';subprocess.run(['git','-C',str(repo),'archive','--format=tar','-o',str(archive),status['revision']],check=True)
 status['archive_sha256']=hashlib.sha256(archive.read_bytes()).hexdigest();record()
 subprocess.run(['scp','-q',str(archive),'<win-user>@<win-host>:'+remote+'/source.tar'],check=True)
 assert ps("Set-Location '"+remote+"'\nif((Get-FileHash source.tar -Algorithm SHA256).Hash.ToLower() -ne '"+status['archive_sha256']+"'){throw 'Hash mismatch'}\ntar -xf source.tar\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake -S . -B build -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON\nexit $LASTEXITCODE",'configure')==0
 status['state']='building';record()
 status['build_exit']=ps("Set-Location '"+remote+"'\ncmake --build build --config Release -j 4\nexit $LASTEXITCODE",'build');record()
 assert status['build_exit']==0
 status['state']='api-inspection';record()
 status['api_exit']=ps("Set-Location '"+remote+"'\n"+"""$env:GST_PLUGIN_PATH=(Get-Location).Path+'\\build\\Release'
$env:PATH=$env:GST_PLUGIN_PATH+';'+$env:PATH
$env:GST_REGISTRY=(Get-Location).Path+'\\api-registry.bin'
Get-FileHash build/Release/framelink.dll,build/Release/framelink_gst_session.dll,build/Release/gstframelink.dll -Algorithm SHA256 | Format-List
foreach($element in @('framelinksink','framelinksrc','framelinkavsink','framelinkavsrc')){gst-inspect-1.0 $element;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}
$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1
if(!$dump){throw 'dumpbin unavailable'}
& $dump.FullName /exports build/Release/framelink.dll
exit $LASTEXITCODE""",'api-runtime');record()
 assert status['api_exit']==0
 status['state']='testing';record()
 status['test_exit']=ps("Set-Location '"+remote+"'\nctest --test-dir build -C Release --output-on-failure\nexit $LASTEXITCODE",'tests');record()
 assert status['test_exit']==0
 status['state']='finished'
except Exception as ex:
 status['state']='failed';status['error']=str(ex);raise
finally:
 status['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();record()
print(json.dumps(status,indent=2))
