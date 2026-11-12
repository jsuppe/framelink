from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-abba3eb'
def ps(code,name,timeout=900):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode

files=['CMakeLists.txt','samples/session_video_relay_events.h','test/fl_gst_native_bin_relay_win.inc','test/fl_gst_video_relay_events.inc','test/fl_gst_native_producer_win.cpp']
assert ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}",'preflight')==0
manifest={}
for name in files:
 path=root/'overlay'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes((repo/name).read_bytes())
 manifest[name]=hashlib.sha256(path.read_bytes()).hexdigest()
 subprocess.run(['scp','-q',str(path),'<win-user>@<win-host>:'+remote+'/'+name],check=True)
(root/'overlay-sha256.json').write_text(json.dumps(manifest,indent=2))
assert ps("Set-Location '"+remote+"'\ncmake -S . -B build -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake --build build --config Release --target fl_gst_native_producer_win -j 4\nexit $LASTEXITCODE",'build')==0
result=ps("Set-Location '"+remote+"'\nctest --test-dir build -C Release --output-on-failure -R '^(gst_native_bin_relay_events_win|gst_video_relay_event_controls_win)$'\nexit $LASTEXITCODE",'targeted')
(root/'status.json').write_text(json.dumps({'test_exit':result,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2))
print('targeted exit',result)
raise SystemExit(result)
