#!/usr/bin/env python3
"""Summarize rates over observed event completion intervals, excluding startup."""
import pathlib,csv,json,statistics,sys,gzip
root=pathlib.Path(sys.argv[1]);out=[]
for d in sorted(root.iterdir()):
 if not d.is_dir():continue
 groups={}
 for p in list(d.glob('*.csv'))+list(d.glob('*.csv.gz')):
  for r in csv.DictReader(x for x in (gzip.decompress(p.read_bytes()).decode() if p.suffix=='.gz' else p.read_text()).splitlines() if not x.startswith('#')):
   if r['operation'] not in ['flSubmit','flAcquireFrame','service.iteration','scene.frame_cpu','gst.sinkRender']:continue
   if r['operation'] in ['flAcquireFrame','flSubmit'] and r['result']!='0':continue
   groups.setdefault((p.name,r['operation']),[]).append(r)
 row={'case':d.name,'operations':[]}
 for (file,op),rs in groups.items():
  if len(rs)<2:continue
  ts=sorted(int(x['start_ns'])+int(x['duration_ns']) for x in rs);dt=sorted((b-a)/1e6 for a,b in zip(ts,ts[1:]));row['operations'].append(dict(file=file,operation=op,count=len(rs),interval_fps=(len(ts)-1)*1e9/(ts[-1]-ts[0]),interval_p50_ms=statistics.median(dt),interval_p99_ms=dt[int(.99*(len(dt)-1))],interval_max_ms=max(dt),duration_p50_ms=statistics.median(int(x['duration_ns']) for x in rs)/1e6))
 if (d/'status.json').exists():
  statuses=json.loads((d/'status.json').read_text());row['final_av']=statuses[-1]['status'].get('av',{}) if statuses else {}
 out.append(row)
(root/'trace-summary.json').write_text(json.dumps(out,indent=2))
for row in out:
 print(row['case'],[(x['operation'],x['count'],round(x['interval_fps'],2)) for x in row['operations']])
