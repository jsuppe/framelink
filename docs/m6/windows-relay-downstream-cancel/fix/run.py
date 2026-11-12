from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-relay-cancel-20261112'
def ps(code,name,timeout=240):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=15','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode
files=['gst/fl_gst_event_channel.hpp','test/fl_gst_event_channel.cpp','test/fl_gst_native_relay_process_win.inc','test/fl_gst_native_producer_win.cpp','CMakeLists.txt']
status={'base':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),'files':{n:hashlib.sha256((repo/n).read_bytes()).hexdigest() for n in files},'state':'starting','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'remote':remote}
def record(): (root/'status.json').write_text(json.dumps(status,indent=2))
record()
try:
 assert ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -like 'framelink*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}",'preflight')==0
 for n in files:subprocess.run(['scp','-q',str(repo/n),'<win-user>@<win-host>:'+remote+'/'+n],check=True)
 status['state']='building';record()
 status['build_exit']=ps("Set-Location '"+remote+"'\ncmake --build build --config Release --target fl_gst_event_channel fl_gst_session_context fl_gst_session_transport fl_gst_native_producer_win gstframelink -j 4\nexit $LASTEXITCODE",'build');record();assert status['build_exit']==0
 status['state']='testing';record()
 status['test_exit']=ps("Set-Location '"+remote+"'\nctest --test-dir build -C Release -R '^gst_(event_channel|session_context|session_transport|native_(producer|relay_.*)_win)$' --output-on-failure -V\nexit $LASTEXITCODE",'tests');record();assert status['test_exit']==0
 status['detail_exit']=ps("Set-Location '"+remote+"'\nGet-ChildItem build/gst-relay-process-*-2.*.log | ForEach-Object {$_.Name; Get-Content $_.FullName}",'detail');record()
 status['state']='finished'
except Exception as ex:
 status['state']='failed';status['error']=str(ex);raise
finally:
 status['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();record()
print(json.dumps(status,indent=2))
