from pathlib import Path
import os,sys,json,subprocess,hashlib,datetime,importlib.util,math
ROOT=Path(__file__).resolve().parent
REPO=Path('<win-home>/dev/framelink-m6-windows-461584b')
BUILD=REPO/'build/Release'
CONFIG=json.loads((ROOT/'config.json').read_text())
assert CONFIG['revision']=='461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff'
FORMAT=CONFIG['format']
assert FORMAT in ['BGRA','NV12']
REV='461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff'
NAMES=['fl_gst_av_bench.exe','gstframelink.dll','framelink_gst_session.dll','framelink.dll']
def hashes():return {n:hashlib.sha256((BUILD/n).read_bytes()).hexdigest() for n in NAMES}
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')
spec=importlib.util.spec_from_file_location('timing',ROOT/'m6_raw_timing.py');timing=importlib.util.module_from_spec(spec);spec.loader.exec_module(timing)
def analyze(folder,seconds):
 label=folder.name
 fields=dict(x.split('=',1) for x in (folder/(label+'.summary.txt')).read_text().splitlines() if '=' in x)
 r={'checks':{},'scope':'shared CPU transport, inferred constant PTS mapping; not physical playback or native allocation identity'}
 check=r['checks']
 for k,v in {'complete':'yes','expected.frames':str(seconds*30),'expected.samples':str(seconds*48000),'consumer.frames':str(seconds*30),'producer.pushed.frames':str(seconds*30),'consumer.samples':str(seconds*48000),'producer.pushed.samples':str(seconds*48000),'consumer.audio.contiguous':'yes','consumer.queue.accounting':'complete','producer.result':'EOS','consumer.result':'EOS','video.pool':'2','video.queue':'1','playback.latency_ms':'0','output.queue_buffers':'1','output.async':'false','output.sync':'false','arrival.budget_ms':'34'}.items():check[k]=fields.get(k)==v
 for track in ['audio','video']:
  for tail in ['pending','unmatched']:check[track+'.'+tail]=fields.get('consumer.'+track+'.output_queue.'+tail)=='0'
 r['video']=timing.analyze(timing.read(folder/(label+'-producer.txt.push.csv')),timing.read(folder/(label+'-consumer.txt.video.csv')),seconds*30,34000000)
 r['audio']=timing.audio_continuity(timing.read(folder/(label+'-consumer.txt.audio.csv')),seconds*100,34000000)
 r['av_mapping']=timing.av_mapping(r['video'],timing.read(folder/(label+'-producer.txt.audio-push.csv')),timing.read(folder/(label+'-consumer.txt.audio.csv')),seconds*100,34000000)
 check['video_34ms']=r['video']['arrival_misses']==0;check['audio_34ms']=r['audio']['arrival_misses']==0
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
if __name__=='__main__':
 s={'revision':REV,'state':'starting','started_utc':stamp(),'binary_sha256':hashes(),'trace':False};save(ROOT/'status.json',s)
 known=json.loads((ROOT/'binary-pins.json').read_text())
 try:
  assert s['binary_sha256']==known,'production integrated binary mismatch'
  env=dict(os.environ)
  for k in list(env):
   if k.upper().startswith(('FRAMELINK_','GST_DEBUG','GST_TRACER')):env.pop(k)
  env['PATH']=str(BUILD)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH'];env['GST_PLUGIN_PATH']=str(BUILD)
  save(ROOT/'environment.json',{k:v for k,v in env.items() if k.startswith(('FRAMELINK','GST_'))})
  for seconds in [5,1800]:
   assert hashes()==s['binary_sha256'],'binary changed between cases'
   label=f'windows-shared-{FORMAT.lower()}-3840x2160-30-{seconds}';folder=ROOT/label;folder.mkdir(exist_ok=False)
   cmd=[str(BUILD/'fl_gst_av_bench.exe'),str(BUILD/'gstframelink.dll'),'--shared-cpu','--seconds',str(seconds),'--size','3840x2160','--fps','30','--format',FORMAT,'--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms','34','--transport-only','--no-output-preroll','--output-queue-buffers','1','--raw','--out',folder.as_posix(),'--label',label]
   save(folder/'command.json',cmd);s.update(state='running',case=label);save(ROOT/'status.json',s);print('START '+label+' '+stamp(),flush=True)
   with (folder/'run.log').open('w') as f:
    proc=subprocess.Popen(cmd,env=env,stdout=f,stderr=subprocess.STDOUT);s['driver_pid']=proc.pid;save(ROOT/'status.json',s);
    try:rc=proc.wait(timeout=seconds+150)
    except subprocess.TimeoutExpired:
     # Only our directly owned process tree; never kill unrelated lane users.
     with (folder/'timeout-cleanup.log').open('w') as cleanup:
      subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],stdout=cleanup,stderr=subprocess.STDOUT,timeout=30)
     raise RuntimeError('owned benchmark exceeded deadline; inspect cleanup and lane before reuse')
   s[label]={'exit':rc,'ended_utc':stamp(),'binary_sha256':hashes()};save(ROOT/'status.json',s)
   assert rc==0,'benchmark failed, no retry'
   assert hashes()==s['binary_sha256'],'binary changed during case'
   assert analyze(folder,seconds),'independent acceptance check failed, no retry'
   with (folder/'independent-local-analysis.json').open('w') as report:
    ar=subprocess.run([sys.executable,'-E',str(ROOT/'analyze_local.py'),str(folder),str(seconds)],stdout=report,stderr=subprocess.STDOUT,timeout=90)
   assert ar.returncode==0,'second independent raw/resource acceptance failed, no retry'
   print('PASS '+label+' '+stamp(),flush=True)
  s['state']='finished'
 except Exception as e:s.update(state='failed',error=str(e))
 finally:s['ended_utc']=stamp();save(ROOT/'status.json',s)
 print(json.dumps(s,indent=2),flush=True);sys.exit(0 if s['state']=='finished' else 1)
