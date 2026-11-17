from pathlib import Path
import json,re,subprocess,math
root=Path(__file__).parent
out={}
def stats(v):
 v=sorted(v);return {k:v[min(len(v)-1,math.ceil(q*len(v))-1)] for k,q in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
for tag in ['a1-row','b1-plane','b2-plane','a2-row']:
 s=json.loads((root/(tag+'-status.json')).read_text());assert s['state']=='finished' and s['exit']==s['pull_exit']==0 and s['affinity_verified']
 d=root/tag
 cmd=['python3','<linux-home>/dev/framelink-m6/tools/m6_raw_timing.py',str(d/'audit-producer.txt.push.csv'),str(d/'audit-consumer.txt.video.csv'),'--expected','3600','--budget-ms','34','--audio',str(d/'audit-consumer.txt.audio.csv'),'--producer-audio',str(d/'audit-producer.txt.audio-push.csv'),'--expected-audio-blocks','12000']
 r=subprocess.run(cmd,capture_output=True,text=True);assert r.returncode in (0,1);c=json.loads(r.stdout)
 assert c==json.loads((root/(tag+'-correlation.json')).read_text())
 f=dict(l.split('=',1) for l in (d/'audit.summary.txt').read_text().splitlines() if '=' in l)
 assert f['complete']=='yes'
 for k in ('consumer.frames','producer.pushed.frames'):assert f[k]=='3600'
 for k in ('consumer.samples','producer.pushed.samples'):assert f[k]=='5760000'
 rows={'consumer':{},'sink':{}};log=(root/(tag+'-run.log')).read_text()
 assert log.count('AUDIT plane_mode enabled='+str(int('plane' in tag)))==1
 assert 'AUDIT output_fallback ' not in log
 for line in log.splitlines():
  for name in rows:
   if line.startswith('AUDIT '+name+' '):
    row={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)};idx=row.get('index',row.get('seq',0)-1)
    assert idx not in rows[name];rows[name][idx]=row
 for v in rows.values():assert set(v)==set(range(3600))
 con=list(rows['consumer'].values());sink=list(rows['sink'].values())
 o={'verified_frames':3600,'video_misses':c['arrival_misses'],'audio':c['audio'],'av_mapping':c['av_mapping'],'arrival_ms':c['arrival_ms'],'consumer_copy_wall_us':stats([r['copy_us'] for r in con]),'consumer_copy_cpu_us':stats([r['copy_cpu_us'] for r in con]),'consumer_copy_wall_minus_cpu_us':stats([r['copy_us']-r['copy_cpu_us'] for r in con]),'sink_copy_us':stats([r['copy_us'] for r in sink]),'sink_wait_us':stats([r['wait_us'] for r in sink]),'copy_fault_sum':sum(r['copy_faults'] for r in con),'fallbacks':0,'late_neighbors':[]}
 for late in c['late_frames']:
  i=late['index'];o['late_neighbors'].append({'timing':late,'stages':{str(j):{n:rows[n][j] for n in rows} for j in range(max(0,i-1),min(3600,i+2))}})
 out[tag]=o
(root/'validated-stage-analysis.json').write_text(json.dumps(out,indent=2)+'\n')
for tag,o in out.items():print(tag,'misses',o['video_misses'],'wall',o['consumer_copy_wall_us'],'cpu',o['consumer_copy_cpu_us'],'off',o['consumer_copy_wall_minus_cpu_us'])
print('all pairwise B p95 reductions', [1-out[b]['consumer_copy_wall_us']['p95']/out[a]['consumer_copy_wall_us']['p95'] for b in ['b1-plane','b2-plane'] for a in ['a1-row','a2-row']])
