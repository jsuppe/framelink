import pathlib,subprocess,base64,json,hashlib,datetime
root=pathlib.Path(__file__).parent;repo=pathlib.Path('<linux-home>/dev/framelink-m6');remote='<win-home>/dev/framelink-m6-windows-32963c5'
s={'base':'32963c51fba63caf768d1dee0cd9aa4bd11a0ad8','remote':remote,'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'files':{}}
def ps(code,name):
 code="$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH;Set-Location '"+remote+"';"+code
 with (root/(name+'.log')).open('w') as f: r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=180)
 s[name]=r.returncode;(root/'status.json').write_text(json.dumps(s,indent=2));return r.returncode
assert ps("if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -like 'fl_*' -or $_.ProcessName -like 'framelink*' -or $_.ProcessName -match 'cmake|ctest|MSBuild'}).Count -ne 0){throw 'Existing executor'}",'preflight')==0
for file in ['samples/session_pcm_relay.h','samples/session_native_relay.h','test/fl_gst_native_relay_av_win.inc','test/fl_gst_native_producer_win.cpp']:
 s['files'][file]=hashlib.sha256((repo/file).read_bytes()).hexdigest();subprocess.run(['scp','-q',str(repo/file),'<win-user>@<win-host>:'+remote+'/'+file],check=True)
assert ps('cmake --build build --config Release --target fl_gst_native_producer_win -j 4;exit $LASTEXITCODE','build')==0
assert ps('& ./build/Release/fl_gst_native_producer_win.exe ./build/Release/gstframelink.dll --relay-av-diagnostic;exit $LASTEXITCODE','diagnostic')==0
