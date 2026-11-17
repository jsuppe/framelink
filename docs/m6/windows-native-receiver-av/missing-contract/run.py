import subprocess,base64,json,datetime,hashlib
from pathlib import Path
root=Path(__file__).parent
repo=Path('<linux-home>/dev/framelink-m6')
remote='<win-home>/dev/framelink-m6-windows-6d7e6be'
status={'base':'6d7e6be7a6a1f6acb3dcdaea0a4c98a55456d985','state':'starting','start':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save(): (root/'status.json').write_text(json.dumps(status,indent=2))
def run(code,name,timeout=600):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+code
 with (root/(name+'.log')).open('w') as out:r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=out,stderr=subprocess.STDOUT,timeout=timeout)
 status[name+'_exit']=r.returncode;save();return r.returncode
save()
try:
 assert run("if(@(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -match '^(fl_|framelink|ctest|cmake|MSBuild)'}).Count){throw 'existing executor'}",'preflight')==0
 status['hashes']={}
 for name in ['fl_gst_native_producer_win.cpp','fl_gst_native_producer_av_win.inc']:
  src=repo/'test'/name;(root/name).write_bytes(src.read_bytes());status['hashes'][name]=hashlib.sha256(src.read_bytes()).hexdigest()
  subprocess.run(['scp','-q',str(src),'<win-user>@<win-host>:'+remote+'/test/'+name],check=True)
 status['state']='build';save()
 assert run('cmake --build build --config Release --target fl_gst_native_producer_win -j 4\nexit $LASTEXITCODE','build')==0
 status['state']='regression';save()
 assert run("ctest --test-dir build -C Release -R '^gst_native_producer_win$' --output-on-failure\nexit $LASTEXITCODE",'regression')==0
 status['state']='finished'
except Exception as e:status['state']='failed';status['error']=str(e);raise
finally:status['end']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
