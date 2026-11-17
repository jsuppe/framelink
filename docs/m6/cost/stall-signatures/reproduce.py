from pathlib import Path
import subprocess,json,time,csv,base64,hashlib
repo=Path('<linux-home>/dev/framelink-m6');root=Path('<linux-home>/dev/evidence/m6-cost/stall-signatures');root.mkdir(exist_ok=True)
s={'state':'running','started':time.time(),'source_sha256':hashlib.sha256((repo/'test/fl_gst_av_bench.cpp').read_bytes()).hexdigest(),'cases':[],'scope':'controlled CPU source/post-push delays; not GPU faults or historical root cause'}
def save():(root/'status.json').write_text(json.dumps(s,indent=2))
def analyze(d,label,stage,code):
 f=dict(l.split('=',1) for l in (d/(label+'.summary.txt')).read_text().splitlines() if '=' in l)
 row={'platform':d.name,'stage':stage,'exit':code,'fields':f};s['cases'].append(row);save()
 assert f.get('consumer.frames')=='150' and f.get('producer.pushed.frames')=='150' and f.get('consumer.samples')=='240000' and f.get('complete')=='yes',label+' incomplete'
 assert f.get('consumer.queue.accounting')=='complete',label+' accounting'
 assert f.get('producer.injection.executed')==('0' if stage=='none' else '1'),label+' injection count'
 if stage!='none':assert int(f['producer.injection.elapsed_us'])>=145000,label+' injection not observed'
 p=list(csv.DictReader((d/(label+'-producer.txt.push.csv')).open()));c=list(csv.DictReader((d/(label+'-consumer.txt.video.csv')).open()));assert len(p)==len(c)==150
 offsets={int(y['pts_ns'])-int(x['pts_ns']) for x,y in zip(p,c)};assert len(offsets)==1,label+' nonconstant PTS mapping'
 ix=89;push=int(p[ix]['push_lateness_ns'])/1e6;arr=int(c[ix]['arrival_lateness_ns'])/1e6;row['fault_frame']={'index':ix,'push_ms':push,'arrival_ms':arr,'post_push_residual_ms':arr-push};save()
 if stage=='source':assert push>=145 and arr-push<34,label+' source signature'
 if stage=='post-push':assert push<34 and arr-push>=145,label+' post-push signature'
save()
try:
 build=Path('<linux-home>/dev/build/framelink-m6-linux');d=root/'linux';d.mkdir(exist_ok=True)
 base=['--seconds','5','--size','3840x2160','--fps','30','--format','NV12','--native','--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms','34','--transport-only','--no-output-preroll','--output-queue-buffers','1','--raw']
 for stage in ['none','source','post-push']:
  label='stall-'+stage;opts=[] if stage=='none' else ['--video-stall-stage',stage,'--video-stall-frame','90','--video-stall-ms','150']
  with (d/(label+'.log')).open('w') as f:r=subprocess.run([str(build/'fl_gst_av_bench'),str(build/'libgstframelink.so'),*base,*opts,'--out',str(d),'--label',label],stdout=f,stderr=subprocess.STDOUT,timeout=90)
  analyze(d,label,stage,r.returncode)
 # Negative configuration must reject before pipeline execution.
 q=subprocess.run([str(build/'fl_gst_av_bench'),str(build/'libgstframelink.so'),'--video-stall-stage','source','--video-stall-frame','0','--video-stall-ms','150'],capture_output=True,text=True,timeout=10);(d/'invalid-injection.log').write_text(q.stdout+q.stderr);assert q.returncode!=0
 remote='<win-home>/dev/framelink-m6-20261106';d=root/'windows';d.mkdir(exist_ok=True)
 old=d/'benchmark.before';subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/test/fl_gst_av_bench.cpp',str(old)],check=True)
 assert old.read_bytes().replace(b'\r\n',b'\n')==subprocess.check_output(['git','show','HEAD:test/fl_gst_av_bench.cpp'],cwd=repo).replace(b'\r\n',b'\n'),'Windows benchmark drift'
 subprocess.run(['scp','-q',str(repo/'test/fl_gst_av_bench.cpp'),'<win-user>@<win-host>:'+remote+'/test/fl_gst_av_bench.cpp'],check=True)
 def ps(code,name):
  p=d/(name+'.ps1');p.write_text("$ErrorActionPreference='Stop'\n$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH\nSet-Location '"+remote+"'\n"+code);target=remote+'/m6-stall-control.ps1';subprocess.run(['scp','-q',str(p),'<win-user>@<win-host>:'+target],check=True)
  with (d/(name+'.log')).open('w') as f:r=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-File',target],stdout=f,stderr=subprocess.STDOUT,timeout=300)
  return r.returncode
 assert ps("if (@(Get-Process fl_gst_av_bench -ErrorAction SilentlyContinue).Count -ne 0) { throw 'Existing benchmark' }\ncmake --build build --config Release --target fl_gst_av_bench -j 4\nexit $LASTEXITCODE",'build')==0
 for stage in ['none','source','post-push']:
  label='m6-stall-'+stage;opts='' if stage=='none' else f'--video-stall-stage {stage} --video-stall-frame 90 --video-stall-ms 150'
  code=ps('./build/Release/fl_gst_av_bench.exe ./build/Release/gstframelink.dll '+' '.join(base)+' '+opts+' --out . --label '+label+'\nexit $LASTEXITCODE',label)
  for suffix in ['.summary.txt','-producer.txt.push.csv','-producer.txt.audio-push.csv','-consumer.txt.video.csv','-consumer.txt.audio.csv']:
   subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/'+label+suffix,str(d/(label+suffix))],check=True)
  analyze(d,label,stage,code)
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='finished' else 1)
