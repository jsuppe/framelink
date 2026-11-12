import subprocess,base64,json,datetime,csv,hashlib
from pathlib import Path
root=Path(__file__).parent
remote='<win-home>/dev/framelink-m6-gap-phase-d8a1f2f'
status=dict(state='starting',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),remote=remote,base_revision='d8a1f2f177d10e6e1c8df108322d58d38b22fb7a',purpose='Diagnostic-only per-stage media hash/output mutex/printf/fflush and appsink pull timing. Four bounded file/RAM paired matrices; no acceptance rerun.')
def save(): (root/'phase-status.json').write_text(json.dumps(status,indent=2))
def ps(code,name,timeout=300):
 code="$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH\n"+code
 with (root/(name+'.log')).open('w') as f:
  return subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
save()
try:
 status['prepare_exit']=ps("""if (@(Get-Process | Where-Object {$_.ProcessName -match 'fl_|framelink|ctest|cmake|MSBuild'}).Count -ne 0){throw 'Existing executor'}
if(Test-Path '"""+remote+"""'){throw 'Existing directory'}
New-Item -ItemType Directory -Path '"""+remote+"""' | Out-Null
Set-Location '"""+remote+"""'
tar -xf <win-home>/dev/framelink-m6-windows-d8a1f2f/source.tar
exit $LASTEXITCODE""",'phase-prepare');save();assert status['prepare_exit']==0
 for src,dst in [('av_apps.instrumented.cpp','samples/av_apps.cpp'),('phase-control.py','phase-control.py')]:
  subprocess.run(['scp','-q',str(root/src),'<win-user>@<win-host>:'+remote+'/'+dst],check=True)
 status['state']='building';save()
 status['build_exit']=ps("Set-Location '"+remote+"'\ncmake -S . -B build -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON\nif($LASTEXITCODE -ne 0){exit $LASTEXITCODE}\ncmake --build build --config Release --target framelink_av_producer framelink_av_consumer gstframelink -j 4\nexit $LASTEXITCODE",'phase-build');save();assert status['build_exit']==0
 status['state']='diagnostic';save()
 status['diagnostic_exit']=ps("Set-Location '"+remote+"'\nforeach($i in 0..3) {python phase-control.py phase-$i;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}}\nexit 0",'phase-diagnostic',240);save()
 status['pull_exits']=[]
 for i in range(4):status['pull_exits'].append(subprocess.run(['scp','-qr','<win-user>@<win-host>:'+remote+'/phase-'+str(i),str(root)]).returncode)
 save();assert status['diagnostic_exit']==0 and not any(status['pull_exits'])
 aggregate=[]
 for i in range(4):
  phase=root/('phase-'+str(i));summary=json.loads((phase/'summary.json').read_text())
  for cell in summary:
   maxima={};dropped=[]
   for f in (phase/(cell['policy']+'-'+cell['stdout'])).glob('app.*.csv'):
    lines=f.read_text().splitlines();dropped += [x for x in lines if x.startswith('# dropped')]
    for row in csv.DictReader(x for x in lines if not x.startswith('#')):
     op=row['operation'];d=int(row['duration_ns'])
     if d>maxima.get(op,{}).get('duration_ns',-1):maxima[op]={'duration_ns':d,'pts_ns':int(row['pts_ns']),'track':row['channel_id'],'file':str(f.relative_to(root))}
   aggregate.append(dict(iteration=i,**cell,phase_maxima=maxima,trace_footer=dropped))
 (root/'phase-analysis.json').write_text(json.dumps(aggregate,indent=2))
 status['state']='collected'
except Exception as e:
 status['state']='failed';status['error']=repr(e);raise
finally:
 status['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
