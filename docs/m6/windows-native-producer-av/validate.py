from pathlib import Path
import subprocess,base64,json,hashlib
root=Path(__file__).parent;root.mkdir(exist_ok=True)
repo=Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-d9585a8'
files=['CMakeLists.txt','test/fl_gst_native_producer_win.cpp','test/fl_gst_native_producer_av_win.inc']
(root/'source-hashes.json').write_text(json.dumps({f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in files},indent=2))
for f in files:subprocess.run(['scp','-q',str(repo/f),'<win-user>@<win-host>:'+remote+'/'+f],check=True)
code=f"""$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PATH='{remote}/build/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
Set-Location '{remote}'
cmake --build build --config Release --target gstframelink fl_gst_native_producer_win -j 4
if($LASTEXITCODE -ne 0){{exit $LASTEXITCODE}}
ctest --test-dir build -C Release --output-on-failure -R '^(gst_native_producer_win|gst_av_bin|gst_shared_cpu_av|gst_pool_policy)$'
exit $LASTEXITCODE
"""
with (root/'windows.log').open('w') as f:r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=300)
(root/'result.json').write_text(json.dumps({'exit':r.returncode}));print(r.returncode)
