from pathlib import Path
import json,csv,statistics,sys,math
root=Path(sys.argv[1]);rows=[]
for case in ['off-1','on-1','off-2','on-2','off-3','on-3']:
 d=root/case;video=[{k:int(v) for k,v in x.items()} for x in csv.DictReader((d/'capture.consumer.csv').open())];assert [x['identity'] for x in video]==list(range(300))
 ages=sorted((x['arrival_ns']-x['source_marker_ns'])/1e6 for x in video)
 item={'case':case,'frames':300,'arrival_age_p95_ms':ages[math.ceil(len(ages)*.95)-1],'resources':{}}
 for role in ['producer','consumer']:item['resources'][role]=json.loads((d/f'capture.resources-{role}.json').read_text())
 rows.append(item)
result={'scope':'ordered three off/on pairs,300frames,1080p60,onehop,GPU verification + testGDP; no universal overhead budget','captures':rows,'paired_deltas':[]}
for off,on in zip(rows[::2],rows[1::2]):
 delta={'pair':off['case'][-1],'arrival_age_p95_ms':on['arrival_age_p95_ms']-off['arrival_age_p95_ms'],'roles':{}}
 for role in ['producer','consumer']:
  a=off['resources'][role];b=on['resources'][role];delta['roles'][role]={k:b[k]-a[k] for k in ['process_cpu_before_flush_ns','peak_resident_before_flush_bytes','explicit_control_flush_ns']}
 result['paired_deltas'].append(delta)
(root/'overhead-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['paired_deltas'],indent=2))
