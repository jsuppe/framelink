#!/usr/bin/env python3
"""Analyze same-host opt-in media probes; source queueing included, not presentation.
Input: output directory from m5_external_pipeline.py with monotonic_ns traces.
"""
import argparse,json,pathlib,statistics
p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=pathlib.Path);a=p.parse_args()
results=[]
def records(path):return [json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')]
for fmt in ('BGRA','NV12'):
    sides=[]
    for side in ('producer','consumer'):
        sides.append([r for r in records(a.directory/(fmt+'-'+side+'.log')) if r.get('event')=='media-trace'])
    for track in ('video','audio'):
        source={r['pts_ns']:r for r in sides[0] if r['track']==track}
        received=[r for r in sides[1] if r['track']==track]
        values=[]
        for r in received:
            s=source[r['pts_ns']]
            assert all(r[k]==s[k] for k in ('sha256','bytes','duration_ns'))
            delta=(r['monotonic_ns']-s['monotonic_ns'])/1e6
            assert delta>=0, 'not comparable host clocks'
            values.append(delta)
        assert values
        values.sort()
        results.append({'format':fmt,'track':track,'matched_blocks':len(values),'source_probe_to_receiver_probe_ms':{'min':values[0],'p50':statistics.median(values),'p99':values[int((len(values)-1)*.99)],'max':values[-1]},'includes_source_queueing_hashing_and_transport':True,'presentation_measured':False})
(a.directory/'latency-summary.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
