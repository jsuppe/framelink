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
s={'state':'starting','started':stamp(),'profile':'explicit retained-device option B, Jon 2026-11-16 criterion: 200 cycles, gate on cycles 100-199; advertised native cells','gate_window':[100,199],'limits':{'memory':16,'handles':.1,'threads':.02},'cases':[]};save(R/'status.json',s)
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
def orig(rows):
 r=rows[5:105];n=len(r);sx=sum(a[0] for a in r);den=n*sum(a[0]**2 for a in r)-sx*sx
 return (n*sum(a[0]*a[1] for a in r)-sx*sum(a[1] for a in r))/den
def slopes(rows):
 rows=rows[100:200];assert len(rows)==100;n=len(rows);sx=sum(a[0] for a in rows);den=n*sum(a[0]**2 for a in rows)-sx*sx
 return {key:(n*sum(a[0]*a[col] for a in rows)-sx*sum(a[col] for a in rows))/den for col,key in [(1,'memory'),(2,'handles'),(3,'threads')]}
try:
 cells=[('nv12-1080p60','1920x1080','60','NV12','17'),('nv12-4k30','3840x2160','30','NV12','34'),('bgra-4k30','3840x2160','30','BGRA','34')]
 def cmd(label,role,cycles,size,fps,fmt,budget):
  return [str(B/'fl_gst_av_bench.exe'),str(B/'gstframelink.dll'),'--device-owner','--native','--cycles',str(cycles),'--seconds','5','--restart',role,'--size',size,'--fps',fps,'--format',fmt,'--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms',budget,'--transport-only','--no-output-preroll','--output-queue-buffers','1','--out',str(R/label),'--label',label]
 def parse(label,cycles,frames):
  d=R/label
  summary=dict(l.split('=',1) for l in (d/(label+'.summary.txt')).read_text().splitlines() if '=' in l)
  assert summary['complete']=='yes' and summary['cycles.ok']==f'{cycles}/{cycles}',label+' incomplete'
  raw=dict(l.split('=',1) for l in (d/(label+'-long.txt')).read_text().splitlines() if '=' in l);rows=[]
  for i in range(cycles):
   a=raw['cycle.'+str(i)].split('|');assert a[:3]==['EOS',str(frames),'240000'],label+' counts '+str(a[:3]);rows.append([i]+[float(v) for v in a[3:]])
  return rows
 for name,size,fps,fmt,budget in cells:
  for role in ('producer','consumer'):
   frames=5*int(fps);label=f'{name}-restart-{role}'
   execute(label+'-smoke',cmd(label+'-smoke',role,2,size,fps,fmt,budget),120);parse(label+'-smoke',2,frames)
 for name,size,fps,fmt,budget in cells:
  for role in ('producer','consumer'):
   frames=5*int(fps);label=f'{name}-restart-{role}'
   execute(label,cmd(label,role,200,size,fps,fmt,budget),2400);rows=parse(label,200,frames)
   fit=slopes(rows);ok=all(math.isfinite(v) and v<=s['limits'][k] for k,v in fit.items())
   result={'label':label,'pass':ok,'slopes':fit,'original_window_5_104':orig(rows),'rows':rows,'ended':stamp()};save(R/label/'analysis.json',result);s['cases'].append(result);save(R/'status.json',s)
 s['state']='finished' if all(c['pass'] for c in s['cases']) else 'failed'
except Exception as e:s.update(state='failed',error=str(e))
finally:
 s.update(ended=stamp(),final_binary_pins=hashes());save(R/'status.json',s)
 files={str(p.relative_to(R)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in R.rglob('*') if p.is_file() and p.name!='remote-sha256.json'};save(R/'remote-sha256.json',files)
print(json.dumps({k:v for k,v in s.items() if k!='cases'}),flush=True)
raise SystemExit(0 if s['state']=='finished' else 1)
