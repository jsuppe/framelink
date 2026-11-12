from pathlib import Path
import subprocess,base64,json,hashlib,datetime
root=Path(__file__).parent;repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-14996d0'
def ps(code,name,timeout=900):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  result=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 return result.returncode

files=['samples/native_gst_d3d11_relay.h','CMakeLists.txt','test/fl_gst_native_bin_relay_win.inc','test/fl_gst_native_producer_av_win.inc','test/fl_gst_native_producer_win.cpp']
for name in files:
 path=root/'overlay'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes((repo/name).read_bytes())
 subprocess.run(['scp','-q',str(path),'<win-user>@<win-host>:'+remote+'/'+name],check=True)
assert ps("Set-Location '"+remote+"'\ncmake -S . -B build -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake --build build --config Release --target fl_gst_native_producer_win -j 4\nexit $LASTEXITCODE",'build')==0
result=ps("Set-Location '"+remote+"'\nctest --test-dir build -C Release --output-on-failure -R '^gst_native_bin_relay_win$'\nexit $LASTEXITCODE",'targeted')
(root/'status.json').write_text(json.dumps({'test_exit':result,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2))
print('targeted exit',result)
