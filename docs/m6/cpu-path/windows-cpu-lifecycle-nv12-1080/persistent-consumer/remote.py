from pathlib import Path
import os,json,subprocess,hashlib,datetime,math
ROOT=Path(__file__).resolve().parent;BUILD=ROOT/'bin'
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def hashes():return {n:hashlib.sha256((BUILD/n).read_bytes()).hexdigest() for n in json.loads((ROOT/'binary-pins.json').read_text())}
s={'state':'starting','started_utc':stamp(),'revision':'461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff','harness_source_sha256':'55573459f141299ee44ad23734bfa273b57f9635114862b16df0a52c75ddf7d8','profile':'shared CPU NV121080p60 restart producer, persistent consumer','limits':{'memory_kib_per_cycle':16,'handles_per_cycle':.1,'threads_per_cycle':.02},'warmup_cycles':5,'cases':[]};save(ROOT/'status.json',s)
try:
 known=json.loads((ROOT/'binary-pins.json').read_text());assert hashes()==known,'binary mismatch'
 env=dict(os.environ)
 for k in list(env):
  if k.upper().startswith(('FRAMELINK_','GST_DEBUG','GST_TRACER')):env.pop(k)
 env['PATH']=str(BUILD)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH'];env['GST_PLUGIN_PATH']=str(BUILD)
 for cycles in [2,100]:
  label='shared-cpu-nv12-1080-restart-producer-'+str(cycles);folder=ROOT/label;folder.mkdir(exist_ok=False)
  cmd=[str(BUILD/'fl_gst_av_bench.exe'),str(BUILD/'gstframelink.dll'),'--shared-cpu','--cycles',str(cycles),'--seconds','5','--restart','producer','--size','1920x1080','--fps','60','--format','NV12','--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms','17','--transport-only','--no-output-preroll','--output-queue-buffers','1','--out',folder.as_posix(),'--label',label]
  save(folder/'command.json',cmd);s.update(state='running',case=label);save(ROOT/'status.json',s)
  with (folder/'run.log').open('w') as log:
   proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT);s['driver_pid']=proc.pid;save(ROOT/'status.json',s)
   try:rc=proc.wait(timeout=cycles*10+120)
   except subprocess.TimeoutExpired:
    subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT,timeout=30);raise RuntimeError('owned lifecycle deadline exceeded; no retry')
  assert hashes()==known,'binary changed'
  fields=dict(l.split('=',1) for l in (folder/(label+'.summary.txt')).read_text().splitlines() if '=' in l)
  checks={'exit':rc==0,'complete':fields.get('complete')=='yes','cycles':fields.get('cycles.ok')==f'{cycles}/{cycles}','frames':fields.get('expected.frames.per_cycle')=='300','pcm':fields.get('expected.samples.per_cycle')=='240000'}
  # Recompute slopes from the persistent-role raw per-cycle records, excluding fixed first5 warmups.
  raw=dict(l.split('=',1) for l in (folder/(label+'-long.txt')).read_text().splitlines() if '=' in l)
  rows=[]
  for i in range(cycles):
   a=raw['cycle.'+str(i)].split('|');assert len(a)==6 and a[:3]==['EOS','300','240000'],'persistent cycle mismatch'
   rows.append([i]+[float(x) for x in a[3:]])
  slopes={}
  if cycles>5:
   steady=rows[5:];n=len(steady);sx=sum(x[0] for x in steady);den=n*sum(x[0]**2 for x in steady)-sx*sx
   for col,key,limit in [(1,'memory_kib_per_cycle',16),(2,'handles_per_cycle',.1),(3,'threads_per_cycle',.02)]:
    slopes[key]=(n*sum(x[0]*x[col] for x in steady)-sx*sum(x[col] for x in steady))/den
    checks[key]=math.isfinite(slopes[key]) and slopes[key]<=limit
  result={'pass':all(checks.values()),'checks':checks,'slopes':slopes,'rows':rows,'scope':'production restart counts and persistent-role resources; not per-frame timing, native lifecycle or physical output'};save(folder/'analysis.json',result)
  s['cases'].append({'label':label,'exit':rc,'pass':result['pass'],'slopes':slopes,'ended_utc':stamp()});save(ROOT/'status.json',s)
  assert result['pass'],'lifecycle gate failed; no retry'
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
finally:s.update(ended_utc=stamp(),final_binary_sha256=hashes());save(ROOT/'status.json',s)
print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='finished' else 1)
