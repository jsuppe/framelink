from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6')
remote='<win-home>/dev/framelink-compat-7b8a274-461584b';current='<win-home>/dev/framelink-m6-windows-461584b'
status={'baseline':'7b8a2741416a59be1fbd66df13a4e88e95bb9bd9','candidate':'461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff','state':'starting'}
def record(): (root/'status.json').write_text(json.dumps(status,indent=2))
def ps(code,name):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:return subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=240).returncode
try:
 record();assert ps("if(@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -match '^(cmake|ctest|MSBuild)$'}).Count){throw 'Runtime busy'}\nif(Test-Path '"+remote+"'){throw 'Existing destination'}\nNew-Item -ItemType Directory '"+remote+"' | Out-Null",'preflight')==0
 archive=root/'baseline.tar';subprocess.run(['git','-C',str(repo),'archive','--format=tar','-o',str(archive),status['baseline']],check=True)
 status['archive_sha256']=hashlib.sha256(archive.read_bytes()).hexdigest();record()
 subprocess.run(['scp','-q',str(archive),'<win-user>@<win-host>:'+remote+'/baseline.tar'],check=True)
 status['state']='building-baseline-clients';record()
 status['build_exit']=ps("Set-Location '"+remote+"'\nif((Get-FileHash baseline.tar -Algorithm SHA256).Hash.ToLower() -ne '"+status['archive_sha256']+"'){throw 'Hash mismatch'}\ntar -xf baseline.tar\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake -S . -B build -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake --build build --config Release --target fl_gst_native_epoch_c_api fl_frame_metadata fl_read_lease_d3d11 -j 4\nexit $LASTEXITCODE",'build');record();assert status['build_exit']==0
 status['imports_exit']=ps("Set-Location '"+remote+"'\n$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1\nif(!$dump){throw 'dumpbin unavailable'}\nforeach($exe in @('fl_gst_native_epoch_c_api','fl_frame_metadata','fl_read_lease_d3d11')){& $dump.FullName /imports ('build/Release/'+$exe+'.exe');if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}\nexit 0",'baseline-imports');record();assert status['imports_exit']==0
 status['state']='baseline-control';record()
 run="foreach($exe in @('fl_gst_native_epoch_c_api','fl_frame_metadata','fl_read_lease_d3d11')){Write-Output $exe;& ('./'+$exe+'.exe');if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}\nexit 0"
 status['baseline_exit']=ps("Set-Location '"+remote+"/build/Release'\n"+run,'baseline-control');record();assert status['baseline_exit']==0
 status['state']='candidate-runtime';record()
 code="Set-Location '"+remote+"'\nNew-Item -ItemType Directory client-runtime | Out-Null\nCopy-Item build/Release/fl_gst_native_epoch_c_api.exe,build/Release/fl_frame_metadata.exe,build/Release/fl_read_lease_d3d11.exe client-runtime/\nCopy-Item '"+current+"/build/Release/framelink.dll','"+current+"/build/Release/framelink_gst_session.dll' client-runtime/\nGet-FileHash build/Release/*.exe,client-runtime/* -Algorithm SHA256 | Format-List\nSet-Location client-runtime\n"+run
 status['candidate_exit']=ps(code,'candidate-runtime');record();assert status['candidate_exit']==0
 status['state']='passed'
except Exception as e:status['state']='failed';status['error']=str(e);raise
finally:status['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat();record()
print(json.dumps(status,indent=2))
