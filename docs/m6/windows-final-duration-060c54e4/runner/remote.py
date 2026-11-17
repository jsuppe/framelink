"""Final-revision duration requalification: 4 shared-CPU + 3 native cells, 5 s preflight then 1800 s. No retries."""
from pathlib import Path
import os,sys,json,subprocess,hashlib,datetime,importlib.util,math
ROOT=Path(__file__).resolve().parent
BUILD=Path('<win-home>/dev/framelink-final-final-060c54e4/build/Release')
REV='060c54e41bcbfe7ca8dbdef89e6e74fb2930935c'
NAMES=['fl_gst_av_bench.exe','fl_gst_local_native_bench_win.exe','gstframelink.dll','framelink_gst_session.dll','framelink.dll']
def hashes():return {n:hashlib.sha256((BUILD/n).read_bytes()).hexdigest() for n in NAMES}
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')
spec=importlib.util.spec_from_file_location('timing',ROOT/'m6_raw_timing.py');timing=importlib.util.module_from_spec(spec);spec.loader.exec_module(timing)
def analyze(folder,seconds,FPS,MS):
 label=folder.name
 fields=dict(x.split('=',1) for x in (folder/(label+'.summary.txt')).read_text().splitlines() if '=' in x)
 r={'checks':{},'scope':'shared CPU transport, inferred constant PTS mapping; not physical playback or native allocation identity'}
 check=r['checks']
 for k,v in {'complete':'yes','expected.frames':str(seconds*FPS),'expected.samples':str(seconds*48000),'consumer.frames':str(seconds*FPS),'producer.pushed.frames':str(seconds*FPS),'consumer.samples':str(seconds*48000),'producer.pushed.samples':str(seconds*48000),'consumer.audio.contiguous':'yes','consumer.queue.accounting':'complete','producer.result':'EOS','consumer.result':'EOS','video.pool':'2','video.queue':'1','playback.latency_ms':'0','output.queue_buffers':'1','output.async':'false','output.sync':'false','arrival.budget_ms':str(MS)}.items():check[k]=fields.get(k)==v
 for track in ['audio','video']:
  for tail in ['pending','unmatched']:check[track+'.'+tail]=fields.get('consumer.'+track+'.output_queue.'+tail)=='0'
 r['video']=timing.analyze(timing.read(folder/(label+'-producer.txt.push.csv')),timing.read(folder/(label+'-consumer.txt.video.csv')),seconds*FPS,MS*1000000)
 r['audio']=timing.audio_continuity(timing.read(folder/(label+'-consumer.txt.audio.csv')),seconds*100,MS*1000000)
 r['av_mapping']=timing.av_mapping(r['video'],timing.read(folder/(label+'-producer.txt.audio-push.csv')),timing.read(folder/(label+'-consumer.txt.audio.csv')),seconds*100,MS*1000000)
 check['video_deadline']=r['video']['arrival_misses']==0;check['audio_deadline']=r['audio']['arrival_misses']==0
 r['resources']={}
 for side in ['consumer','producer']:
  series=[[float(x) for x in row.split(':')] for row in fields[side+'.series.t_cpums_memkb_handles_threads'].strip(';').split(';')]
  steady=[row for row in series[:-1] if row[0]>=2];half=steady[len(steady)//2:]
  def slope(col):
   n=len(half);sx=sum(x[0] for x in half);sy=sum(x[col] for x in half);den=n*sum(x[0]**2 for x in half)-sx*sx
   return (n*sum(x[0]*x[col] for x in half)-sx*sy)/den*60 if den>0 else 0
  v={'mem_kb_per_min':slope(2),'handles_per_min':slope(3),'threads_per_min':slope(4),'cpu_percent':float(fields[side+'.cpu.percent']),'sample_count':len(series),'rounded_series':True}
  r['resources'][side]=v
  if seconds>=600:
   for k,limit in [('mem_kb_per_min',64),('handles_per_min',1),('threads_per_min',.5)]:check[side+'.'+k]=math.isfinite(v[k]) and v[k]<=limit
 r['pass']=all(check.values());save(folder/'independent-analysis.json',r);return r['pass']

SHARED=[('nv12','NV12',1920,1080,60,17),('bgra','BGRA',1920,1080,60,17),('nv12','NV12',3840,2160,30,34),('bgra','BGRA',3840,2160,30,34)]
NATIVE=[('NV12',1920,1080,60),('NV12',3840,2160,30),('BGRA',3840,2160,30)]
s={'revision':REV,'state':'starting','started_utc':stamp(),'binary_sha256':hashes(),'cells':[]};save(ROOT/'status.json',s)
env=dict(os.environ)
for k in list(env):
 if 'TRACE' in k.upper() or k.upper().startswith(('FRAMELINK_','GST_DEBUG','GST_TRACER','FL_BENCH_','FL_FAULT')):env.pop(k)
env['PATH']=str(BUILD)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH'];env['GST_PLUGIN_PATH']=str(BUILD);env['GST_REGISTRY']=str(ROOT/'gst-registry.bin')
def idle():
 out=subprocess.run(['powershell','-NoProfile','-Command',"@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$'}).Count"],capture_output=True,text=True,timeout=60).stdout.strip()
 return out=='0'
def execute(label,cmd,timeout,native=False):
 folder=ROOT/label
 if not cmd[2:3]==['--out']:folder.mkdir(exist_ok=False)
 else:folder.parent.mkdir(exist_ok=True)
 meta=ROOT/(label+'.meta');meta.mkdir(exist_ok=False);save(meta/'command.json',cmd)
 s.update(state='running',case=label);save(ROOT/'status.json',s);print('START '+label+' '+stamp(),flush=True)
 assert idle(),'runtime busy before '+label
 assert hashes()==s['binary_sha256'],'binary changed before '+label
 with (meta/'run.log').open('w') as f:
  p=subprocess.Popen(cmd,env={k:v for k,v in env.items() if not(native and k=='GST_PLUGIN_PATH')},cwd=ROOT,stdout=f,stderr=subprocess.STDOUT);s['driver_pid']=p.pid;save(ROOT/'status.json',s)
  try:rc=p.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=f,stderr=subprocess.STDOUT,timeout=30);rc=-999
 assert hashes()==s['binary_sha256'],'binary changed during '+label
 return rc,meta
def shared(tag,fmt,w,h,fps,ms,seconds):
 label=f'windows-shared-{tag}-{w}x{h}-{fps}-{seconds}'
 cmd=[str(BUILD/'fl_gst_av_bench.exe'),str(BUILD/'gstframelink.dll'),'--shared-cpu','--seconds',str(seconds),'--size',f'{w}x{h}','--fps',str(fps),'--format',fmt,'--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms',str(ms),'--transport-only','--no-output-preroll','--output-queue-buffers','1','--raw','--out',(ROOT/label).as_posix(),'--label',label]
 rc,meta=execute(label,cmd,seconds+150);ok=rc==0;second=None
 if ok:ok=analyze(ROOT/label,seconds,fps,ms)
 if rc==0:
  with (meta/'independent-local-analysis.json').open('w') as rep:second=subprocess.run([sys.executable,'-E',str(ROOT/('analyze_local_4k30.py' if fps==30 else 'analyze_local_1080p60.py')),str(ROOT/label),str(seconds)],stdout=rep,stderr=subprocess.STDOUT,timeout=180).returncode
  ok=ok and second==0
 return {'label':label,'kind':'shared-cpu','seconds':seconds,'exit':rc,'second_analysis_exit':second,'pass':bool(ok),'ended_utc':stamp()}
def native(fmt,w,h,fps,seconds):
 label=f'windows-native-{fmt.lower()}-{w}x{h}-{fps}-{seconds}'
 cmd=[str(BUILD/'fl_gst_local_native_bench_win.exe'),str(BUILD/'gstframelink.dll'),'--out',str(ROOT/label),'--seconds',str(seconds),'--width',str(w),'--height',str(h),'--fps',str(fps),'--format',fmt]
 rc,meta=execute(label,cmd,seconds+150,True);check=None
 if rc==0:
  with (meta/'analysis.log').open('w') as rep:check=subprocess.run([sys.executable,'-E',str(ROOT/'check_local_native_bench.py'),str(ROOT/label)],stdout=rep,stderr=subprocess.STDOUT,timeout=300).returncode
 return {'label':label,'kind':'native','seconds':seconds,'exit':rc,'analysis_exit':check,'pass':rc==0 and check==0,'ended_utc':stamp()}
try:
 pins=json.loads((ROOT/'dll-pins.json').read_text())
 assert all(s['binary_sha256'][n]==pins[n] for n in pins),'DLLs differ from the final integrated run'
 def record(r):
  s['cells'].append(r);save(ROOT/'status.json',s);print(('PASS ' if r['pass'] else 'FAIL ')+r['label']+' '+stamp(),flush=True)
 pre=[]
 for c in SHARED:
  try:r=shared(*c,5)
  except Exception as e:r={'label':'shared-preflight-'+c[0]+str(c[4]),'pass':False,'error':str(e)}
  record(r);pre.append(r['pass'])
 for c in NATIVE:
  try:r=native(*c,5)
  except Exception as e:r={'label':'native-preflight-'+c[0]+str(c[3]),'pass':False,'error':str(e)}
  record(r);pre.append(r['pass'])
 assert all(pre),'preflight failed; no duration cells started'
 for c in SHARED:
  try:r=shared(*c,1800)
  except Exception as e:r={'label':'shared-'+c[0]+str(c[4])+'-1800','pass':False,'error':str(e)}
  record(r)
 for c in NATIVE:
  try:r=native(*c,1800)
  except Exception as e:r={'label':'native-'+c[0]+str(c[3])+'-1800','pass':False,'error':str(e)}
  record(r)
 s['state']='finished' if all(c['pass'] for c in s['cells']) else 'failed'
except Exception as e:s.update(state='failed',error=str(e))
finally:
 s.update(ended_utc=stamp(),final_binary_sha256=hashes(),idle_final=idle());save(ROOT/'status.json',s)
 files={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in ROOT.rglob('*') if p.is_file() and p.name not in ('remote-sha256.json','gst-registry.bin')};save(ROOT/'remote-sha256.json',files)
print(json.dumps({k:v for k,v in s.items() if k!='cells'}),flush=True);sys.exit(0 if s['state']=='finished' else 1)
