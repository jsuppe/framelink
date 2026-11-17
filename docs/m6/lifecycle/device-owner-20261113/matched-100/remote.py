from pathlib import Path
import os,subprocess,json,hashlib,datetime,math
R=Path(__file__).resolve().parent;B=R.parent/'build'/'Release'
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(p,d):p.write_text(json.dumps(d,indent=2)+'\n')
names=['framelink.dll','framelink_gst_session.dll','gstframelink.dll','fl_gst_av_bench.exe','fl_read_leases.exe']
def hashes():return {n:hashlib.sha256((B/n).read_bytes()).hexdigest() for n in names}
pins=hashes();save(R/'binary-pins.json',pins)
env=dict(os.environ);env['PATH']=str(B)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH'];env['GST_PLUGIN_PATH']=str(B)
for k in list(env):
 if k.upper().startswith(('FRAMELINK_','GST_DEBUG','GST_TRACER')):env.pop(k)
s={'state':'starting','started':stamp(),'profile':'explicit retained-device option B','warmup':5,'limits':{'memory':16,'handles':.1,'threads':.02},'cases':[]};save(R/'status.json',s)
def execute(label,cmd,timeout):
 d=R/label;d.mkdir();save(d/'command.json',cmd);s.update(state='running',case=label);save(R/'status.json',s)
 with (d/'run.log').open('w') as log:
  p=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT);s['pid']=p.pid;save(R/'status.json',s)
  try:rc=p.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT);raise RuntimeError('owned deadline expired; no retry')
 assert hashes()==pins,'binaries changed'
 assert rc==0,f'{label} exit {rc}'
 return d
def slopes(rows):
 rows=rows[5:];n=len(rows);sx=sum(a[0] for a in rows);den=n*sum(a[0]**2 for a in rows)-sx*sx
 return {key:(n*sum(a[0]*a[col] for a in rows)-sx*sum(a[col] for a in rows))/den for col,key in [(1,'memory'),(2,'handles'),(3,'threads')]}
try:
 for role in ['producer','consumer']:
  label='restart-'+role;d=R/label
  cmd=[str(B/'fl_gst_av_bench.exe'),str(B/'gstframelink.dll'),'--device-owner','--native','--cycles','100','--seconds','5','--restart',role,'--size','1920x1080','--fps','60','--format','BGRA','--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms','17','--transport-only','--no-output-preroll','--output-queue-buffers','1','--out',str(d),'--label',label]
  execute(label,cmd,1120)
  summary=dict(l.split('=',1) for l in (d/(label+'.summary.txt')).read_text().splitlines() if '=' in l)
  assert summary['complete']=='yes' and summary['cycles.ok']=='100/100'
  raw=dict(l.split('=',1) for l in (d/(label+'-long.txt')).read_text().splitlines() if '=' in l);rows=[]
  for i in range(100):
   a=raw['cycle.'+str(i)].split('|');assert a[:3]==['EOS','300','240000'];rows.append([i]+[float(v) for v in a[3:]])
  fit=slopes(rows);ok=all(math.isfinite(v) and v<=s['limits'][k] for k,v in fit.items());result={'label':label,'pass':ok,'slopes':fit,'rows':rows,'ended':stamp()};save(d/'analysis.json',result);s['cases'].append(result);save(R/'status.json',s);assert ok,'original resource gate failed'
 d=execute('held-leases',[str(B/'fl_read_leases.exe'),'--cycles','100','--device-owner'],120)
 rows=[]
 for line in (d/'run.log').read_text().splitlines():
  if line.startswith('cycle.'):
   key,val=line.split('=');rows.append([int(key[6:])]+[float(x) for x in val.split('|')])
 assert len(rows)==100;fit=slopes(rows);ok=all(math.isfinite(v) and v<=s['limits'][k] for k,v in fit.items());result={'label':'held-leases','pass':ok,'slopes':fit,'rows':rows,'ended':stamp()};save(d/'analysis.json',result);s['cases'].append(result);assert ok,'held lease resource gate failed'
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
finally:
 s.update(ended=stamp(),final_binary_pins=hashes());save(R/'status.json',s)
 files={str(p.relative_to(R)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in R.rglob('*') if p.is_file() and p.name!='remote-sha256.json'};save(R/'remote-sha256.json',files)
print(json.dumps({k:v for k,v in s.items() if k!='cases'}),flush=True)
raise SystemExit(0 if s['state']=='finished' else 1)
