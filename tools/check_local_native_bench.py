#!/usr/bin/env python3
"""Fail-closed independent accounting for authenticated local native relay captures.
17/34ms are whole-path arrival diagnostics, not approved isolated-hop budgets.
"""
import argparse,csv,json,math,pathlib,re,statistics

def require(ok,message):
    if not ok: raise ValueError(message)
def read(path):
    with path.open() as f:return [{k:int(v) for k,v in row.items()} for row in csv.DictReader(f)]
def distribution(values):
    values=sorted(values)
    return {key:values[max(0,math.ceil(q*len(values))-1)]/1e6 for key,q in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
def resource_analysis(resources,seconds,role):
    require(len(resources)>=2 and all(math.isfinite(x) for r in resources for x in r.values()),role+' missing/nonfinite resources')
    require(all(b['t_ns']>a['t_ns'] for a,b in zip(resources,resources[1:])),role+' nonmonotonic resource clock')
    require(all(r[k]>=0 for r in resources for k in ('t_ns','cpu_ns','memory_kb','handles','threads')),role+' negative resource counter')
    require(all(b['cpu_ns']>=a['cpu_ns'] for a,b in zip(resources,resources[1:])),role+' decreasing CPU counter')
    slopes={}
    if seconds>=600:
        require(resources[-1]['t_ns']-resources[0]['t_ns']>=(seconds-2)*10**9,role+' truncated resources')
        steady=[r for r in resources[:-1] if r['t_ns']>=resources[0]['t_ns']+2*10**9];tail=steady[len(steady)//2:]
        require(len(tail)>=10,role+' insufficient resource samples')
        xs=[(r['t_ns']-tail[0]['t_ns'])/1e9 for r in tail];mx=statistics.mean(xs);den=sum((x-mx)**2 for x in xs)
        for key,limit in [('memory_kb',64),('handles',1),('threads',.5)]:
            ys=[r[key] for r in tail];my=statistics.mean(ys);slope=sum((x-mx)*(y-my) for x,y in zip(xs,ys))/den*60
            require(math.isfinite(slope) and slope<=limit,role+' '+key+' slope gate');slopes[key]=slope
    return slopes,(resources[-1]['cpu_ns']-resources[0]['cpu_ns'])/(resources[-1]['t_ns']-resources[0]['t_ns'])*100
def analyze(root):
    root=pathlib.Path(root);c=json.loads((root/'config.json').read_text());seconds=c['seconds'];fps=c['fps'];n=seconds*fps;audio=seconds*100
    require(1<=seconds<=1800 and fps in (30,60),'invalid frozen duration/rate')
    require(c['pool']==2 and c['queue']==1 and c['offset_ns']==37000000,'profile changed')
    # Pre-roll: marked frames below the measured timeline (PTS < 37 ms) before
    # the paced epoch, never in the CSVs. 0 for captures made before it existed.
    preroll=c.get('preroll',0);require(preroll in (0,8+fps//2),'profile changed (pre-roll)')
    require(c['format'] in ('BGRA','RGBA','NV12') and c['width']>=64 and c['height']>=32,'invalid geometry')
    require(c['clock'] in ('QPC quotient-remainder nanoseconds','CLOCK_MONOTONIC nanoseconds'),'unexpected clock domain')
    budget=17000000 if fps==60 else 34000000;expected_bytes=c['width']*c['height']*(3 if c['format']=='NV12' else 8)//2
    videos={};audios={};result={'scope':'synthetic authenticated transport; explicit upload/verification copies; no physical playback or isolated-hop budget claim','budget_ns':budget,'roles':{}}
    for role in ('source','relay','receiver'):
        v=read(root/(role+'.video.csv'));a=read(root/(role+'.audio.csv'));videos[role]=v;audios[role]=a
        require([r['identity'] for r in v]==list(range(n)),role+' missing/duplicate/reordered video identity')
        require([r['identity'] for r in a]==list(range(audio)),role+' missing/duplicate/reordered PCM identity')
        require(all(r['pts_ns']==37000000+i*10**9//fps for i,r in enumerate(v)),role+' video timeline')
        require(all(r['pts_ns']==i*10000000 and r['samples']==480 and r['bytes']==1920 for i,r in enumerate(a)),role+' PCM timeline/size')
        require(all(r['metadata_version']==1 and r['metadata_flags']==3 and r['generation']>0 and r['dts_ns']==r['pts_ns'] and r['duration_ns']==(i+1)*10**9//fps-i*10**9//fps for i,r in enumerate(v)),role+' metadata')
        if role=='receiver':
            require(all(r['lease']>0 and r['completion_value']>0 for r in v) and all(b['completion_value']>a['completion_value'] for a,b in zip(v,v[1:])) and (preroll or all(r['completion_value']==i+1 for i,r in enumerate(v))), 'missing actual lease/fence completion evidence')
            require(len({r['lease'] for r in v})==n,'duplicate receiver lease')
        require(all(r['observed_pts_ns']==r['mapped_pts_ns'] for r in a),role+' actual PCM PTS differs from session mapping')
        offset=a[0]['observed_pts_ns']-a[0]['pts_ns']
        require(all(r['observed_pts_ns']-r['pts_ns']==offset for r in a),role+' changing audio mapping')
        require(all(r['mapped_pts_ns']-r['pts_ns']==offset for r in v),role+' video/audio mapping mismatch')
        source_phases=('acquire_begin_ns','acquired_ns','upload_begin_ns','uploaded_ns','submit_begin_ns','submitted_ns')
        relay_phases=('forward_begin_ns','forwarded_ns')
        for row in v:
            if role=='source':
                require(0<row['acquire_begin_ns']<=row['acquired_ns']<=row['source_ns']<=row['upload_begin_ns']<=row['uploaded_ns']<=row['submit_begin_ns']<=row['submitted_ns']<=row['arrival_ns'],role+' invalid source phase order')
            else:require(all(row[k]==0 for k in source_phases),role+' unexpected source phases')
            if role=='relay':
                require(row['arrival_ns']<=row['forward_begin_ns']<=row['forwarded_ns']<=row['completed_ns'],role+' invalid forward phase order')
            else:require(all(row[k]==0 for k in relay_phases),role+' unexpected relay phases')
        require(all(r['copy_bytes']==(0 if role=='relay' else expected_bytes) for r in v),role+' copy accounting')
        s=json.loads((root/(role+'.summary.json')).read_text())
        require(s['terminal'] is True and s['video']==n and s['audio_blocks']==audio and s['pcm_frames']==seconds*48000,role+' terminal accounting')
        if role!='source':require(s['held_last_proof'] is True,role+' held final lease proof')
        with (root/(role+'.resources.csv')).open() as f:resources=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(f)]
        slopes,cpu=resource_analysis(resources,seconds,role)
        result['roles'][role]={'video':len(v),'pcm_frames':sum(r['samples'] for r in a),'resource_slopes_per_min':slopes,'cpu_percent':cpu}
    late=[]
    for i,(s,m,r) in enumerate(zip(videos['source'],videos['relay'],videos['receiver'])):
        require(s['generation']==m['generation']==r['generation'],'cross-hop generation mismatch')
        require(s['scheduled_ns']>0 and s['scheduled_ns']<=s['source_ns']<=m['arrival_ns']<=m['completed_ns'],'video source/relay/completion causal order')
        require(r['source_ns']==s['source_ns'] and r['scheduled_ns']==s['scheduled_ns'],'changed source stamp')
        require(r['arrival_ns']>=m['arrival_ns'] and r['completed_ns']>=r['arrival_ns'],'premature receiver completion')
        require(s['scheduled_ns']-videos['source'][0]['scheduled_ns']==i*10**9//fps,'video source cadence')
        if r['arrival_ns']-s['scheduled_ns']>budget:late.append(i)
    audio_late=[]
    for i,(s,m,r) in enumerate(zip(audios['source'],audios['relay'],audios['receiver'])):
        require(s['source_ns']==m['source_ns']==r['source_ns'] and s['scheduled_ns']==m['scheduled_ns']==r['scheduled_ns'],'changed PCM stamp')
        require(s['scheduled_ns']<=s['source_ns']<=m['arrival_ns']<=r['arrival_ns'],'PCM causal order')
        require(s['scheduled_ns']-audios['source'][0]['scheduled_ns']==i*10000000,'PCM source cadence')
        if r['arrival_ns']-s['scheduled_ns']>budget:audio_late.append(i)
    require(videos['source'][0]['scheduled_ns']==audios['source'][0]['scheduled_ns'],'source A/V epoch differs')
    require(not late and not audio_late,'arrival diagnostic misses: video='+str(late[:10])+' audio='+str(audio_late[:10]))
    result['timing_ms']={
        'scheduled_to_final_arrival':distribution([r['arrival_ns']-s['scheduled_ns'] for s,r in zip(videos['source'],videos['receiver'])]),
        'source_marker_to_relay':distribution([r['arrival_ns']-s['source_ns'] for s,r in zip(videos['source'],videos['relay'])]),
        'source_marker_to_final':distribution([r['arrival_ns']-s['source_ns'] for s,r in zip(videos['source'],videos['receiver'])]),
        'source_acquire_call':distribution([r['acquired_ns']-r['acquire_begin_ns'] for r in videos['source']]),
        'source_upload_call':distribution([r['uploaded_ns']-r['upload_begin_ns'] for r in videos['source']]),
        'source_submit_call':distribution([r['submitted_ns']-r['submit_begin_ns'] for r in videos['source']]),
        'relay_acquire_to_record':distribution([r['completed_ns']-r['arrival_ns'] for r in videos['relay']]),
        'relay_forward_call':distribution([r['forwarded_ns']-r['forward_begin_ns'] for r in videos['relay']]),
        'final_acquire_to_completion':distribution([r['completed_ns']-r['arrival_ns'] for r in videos['receiver']]),
        'audio_scheduled_to_final':distribution([r['arrival_ns']-s['scheduled_ns'] for s,r in zip(audios['source'],audios['receiver'])])}
    if c['trace']:
        channels={};sends={};receives={}
        for role in ('source','relay','receiver'):
            paths=list(root.glob(role+'.trace.csv.*.csv'));require(len(paths)==1,role+' missing/ambiguous trace')
            lines=paths[0].read_text().splitlines();foot=[x for x in lines if x.startswith('# dropped_events=')]
            require(len(foot)==1 and re.match(r'# dropped_events=0,rejected_threads=0,',foot[0]),role+' trace loss')
            records=list(csv.DictReader(x for x in lines if not x.startswith('#')))
            boundary=lambda r:r['result']=='0' and r['operation'] in ('flEndSubmit','flAcquireFrame')
            early=[r for r in records if boundary(r) and int(r['pts_ns'])<37000000]
            pre_sent=sum(r['operation']=='flEndSubmit' for r in early);pre_received=sum(r['operation']=='flAcquireFrame' for r in early)
            require({'source':pre_sent==preroll and pre_received==0,'relay':pre_sent==pre_received==preroll,'receiver':pre_sent==0 and pre_received<=preroll}[role],role+' trace pre-roll counts')
            records=[r for r in records if not (boundary(r) and int(r['pts_ns'])<37000000)]
            maps=[r for r in records if r['operation'] in ('flImageMap','flImageMapPlanes')]
            require(role!='relay' or not maps,'relay payload map')
            if role=='relay':require(not any(int(r.get('payload_bytes',0))>0 or r['operation'] in ('image.gpu_readback_issued','image.gpu_upload_issued') for r in records),'relay payload copy')
            sent=sum(r['operation']=='flEndSubmit' and r['result']=='0' for r in records)
            received=sum(r['operation']=='flAcquireFrame' and r['result']=='0' for r in records)
            require((sent,received)=={'source':(n,0),'relay':(n,n),'receiver':(0,n)}[role],role+' trace role counts')
            result['roles'][role]['trace_counts']={'sent':sent,'received':received,'preroll_sent':pre_sent,'preroll_received':pre_received}
            for r in records:
                if r['result']!='0' or r['operation'] not in ('flEndSubmit','flAcquireFrame'):continue
                k=(r['channel_id'],int(r['fence']),int(r['pts_ns']));d=sends if r['operation']=='flEndSubmit' else receives
                require(k not in d,'duplicate trace identity');d[k]=r;channels.setdefault(k[0],set()).add(k[2])
        require(sends.keys()==receives.keys() and len(sends)==2*n,'trace boundary identities')
        require(len(channels)==2 and all(v=={37000000+i*10**9//fps for i in range(n)} for v in channels.values()),'trace timeline')
    result.update(pass_all=True,video_arrival_misses=0,audio_arrival_misses=0,trace_checked=c['trace'],preroll=preroll);return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory');a=p.parse_args()
    try:r=analyze(a.directory)
    except (ValueError,KeyError,OSError,TypeError,ZeroDivisionError) as e:print(json.dumps({'pass_all':False,'error':str(e)}));return 1
    print(json.dumps(r,indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
