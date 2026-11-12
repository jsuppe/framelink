#!/usr/bin/env python3
"""Independent artifact gates for the authenticated decoder-rate fixture.
Full active-row digests are joined offline; no synthetic bytes replace video pixels.
"""
import argparse,csv,json,math,pathlib,re,statistics

def require(ok,message):
    if not ok:raise ValueError(message)
def read(path):
    with path.open() as f:return [{k:(v if k=='digest' else int(v)) for k,v in row.items()} for row in csv.DictReader(f)]
def distribution(values):
    values=sorted(values)
    return {key:values[max(0,math.ceil(q*len(values))-1)]/1e6 for key,q in [('p50',.5),('p95',.95),('p99',.99),('max',1)]}
def resource_analysis(rows,seconds):
    require(len(rows)>=2 and all(math.isfinite(v) and v>=0 for r in rows for v in r.values()),'missing/nonfinite/negative resource counter')
    require(all(b['t_ns']>a['t_ns'] and b['cpu_ns']>=a['cpu_ns'] for a,b in zip(rows,rows[1:])),'resource counter regression')
    result={'cpu_percent':100*(rows[-1]['cpu_ns']-rows[0]['cpu_ns'])/(rows[-1]['t_ns']-rows[0]['t_ns']),'samples':len(rows),'slopes_per_min':{}}
    if seconds>=600:
        require(rows[-1]['t_ns']-rows[0]['t_ns']>=(seconds-2)*10**9,'truncated resource interval')
        steady=[r for r in rows[:-1] if r['t_ns']>=rows[0]['t_ns']+2*10**9];tail=steady[len(steady)//2:]
        require(len(tail)>=10,'insufficient steady resources')
        xs=[(r['t_ns']-tail[0]['t_ns'])/1e9 for r in tail];mx=statistics.mean(xs);den=sum((x-mx)**2 for x in xs)
        for key,limit in [('memory_kb',64),('handles',1),('threads',.5)]:
            ys=[r[key] for r in tail];my=statistics.mean(ys);slope=60*sum((x-mx)*(y-my) for x,y in zip(xs,ys))/den
            require(math.isfinite(slope) and slope<=limit,'resource slope gate: '+key);result['slopes_per_min'][key]=slope
    return result

def trace_analysis(root,c,video):
    n=c['expected_video'];capacity=c['seconds']*2500+n*32+1024
    require(c['seconds']<=5 and c.get('trace_capacity_per_thread')==capacity,'trace duration/capacity contract')
    boundaries={};counts={}
    for role in ('source','relay','receiver'):
        paths=list(root.glob(role+'.trace.csv.*.csv'))
        require(len(paths)==1,role+' missing/ambiguous trace')
        lines=paths[0].read_text().splitlines()
        require(len([x for x in lines if x.startswith('# schema=2,clock=steady_ns,')])==1,role+' trace schema/clock')
        footer=[x for x in lines if x.startswith('# dropped_events=')]
        require(footer==[f'# dropped_events=0,rejected_threads=0,capacity_per_thread={capacity}'],role+' trace loss/capacity')
        records=list(csv.DictReader(x for x in lines if not x.startswith('#')))
        if role=='relay':
            require(not any(r['operation'] in ('flImageMap','flImageMapPlanes') for r in records),'relay payload map')
            require(not any(int(r['payload_bytes'])!=0 or r['operation'] in ('image.gpu_readback_issued','image.gpu_upload_issued') for r in records),'relay payload copy')
        counts[role]={}
        for op,label in [('flEndSubmit','sent'),('flAcquireFrame','received')]:
            rows=[r for r in records if r['operation']==op and r['result']=='0']
            expected=0 if (role=='source' and label=='received') or (role=='receiver' and label=='sent') else n
            require(len(rows)==expected,role+' trace role counts');counts[role][label]=len(rows)
            if not rows:continue
            require(len({r['channel_id'] for r in rows})==1,role+' trace mixed channel')
            bypts={int(r['pts_ns']):r for r in rows}
            require(len(bypts)==n and set(bypts)=={v['pts_ns'] for v in video[role]},role+' trace PTS identities')
            for v in video[role]:
                r=bypts[v['pts_ns']]
                # Submit scopes expose slot/fence but not sequence. Receive scopes expose all three.
                if label=='received':
                    require((int(r['slot']),int(r['fence']),int(r['sequence']))==(v['slot'],v['ready_value'],v['sequence']),role+' trace CSV native identity')
                elif role=='source':require(int(r['slot'])==v['slot'],'source trace CSV slot')
            boundaries[(role,label)]=bypts
    up=boundaries[('source','sent')];up_in=boundaries[('relay','received')]
    down=boundaries[('relay','sent')];down_in=boundaries[('receiver','received')]
    require(next(iter(up.values()))['channel_id']!=next(iter(down.values()))['channel_id'],'trace requires two distinct channels')
    for sent,received in [(up,up_in),(down,down_in)]:
        for pts,r in sent.items():
            other=received[pts]
            require(all(r[k]==other[k] for k in ('channel_id','fence','slot','pts_ns')),'trace boundary identities')
    return counts

def analyze(root):
    root=pathlib.Path(root);c=json.loads((root/'config.json').read_text())
    require(c['schema']==1 and c['format']=='RGBA' and type(c['trace']) is bool,'unsupported schema/profile/trace claim')
    require(c['clock']=='QPC quotient-remainder nanoseconds' and c['digest']=='SHA256 active rows','clock/digest contract')
    seconds=c['seconds'];fps=c['fps'];n=seconds*fps;audio=seconds*100;size=c['width']*c['height']*4
    require(isinstance(seconds,int) and 1<=seconds<=1800,'duration')
    require((c['width'],c['height'],fps) in [(64,32,30),(1920,1080,60),(3840,2160,30)],'unsupported target')
    require((c['decoder_width'],c['decoder_height'])==((320,240) if c['width']==64 else (c['width'],c['height'])),'decoder input not target size')
    require(c['expected_video']==n and c['expected_audio_blocks']==audio and c['expected_pcm_frames']==seconds*48000,'configured expected counts inconsistent')
    require(c['pool']==2 and c['queue']==1 and c['offset_ns']==37000000,'native pool/queue/timeline profile')
    budget=17000000 if fps==60 else 34000000
    video={};pcm={};result={'scope':'decoder conversion plus source/final full-frame readback workload; no physical renderer or isolated-hop budget claim','trace_qualified':False,'budget_ns':budget,'resources':{}}
    source_fields=('decoded_pts_ns','decoded_dts_ns','decoded_origin_ns','scheduled_ns','acquire_begin_ns','acquired_ns','decode_begin_ns','decoded_ns','convert_begin_ns','converted_ns','conversion_done_ns','oracle_begin_ns','oracle_done_ns','submit_begin_ns','submitted_ns')
    final_fields=('copy_begin_ns','signalled_ns','gpu_done_ns','lease_done_ns','hash_begin_ns','hash_done_ns','lease','completion_value')
    relay_fields=('forward_begin_ns','forwarded_ns','native_matches')
    for role in ('source','relay','receiver'):
        v=read(root/(role+'.video.csv'));a=read(root/(role+'.audio.csv'));video[role]=v;pcm[role]=a
        require([r['identity'] for r in v]==list(range(n)),role+' video identities/count')
        require([r['identity'] for r in a]==list(range(audio)),role+' PCM identities/count')
        for i,r in enumerate(v):
            require(r['pts_ns']==37000000+i*10**9//fps,role+' video PTS')
            require(r['metadata_version']==1 and r['metadata_flags']==2 and r['dts_ns']==0 and r['duration_ns']==10**9//fps and r['generation']>0,role+' metadata/duration/DTS')
            require(r['slot'] in (0,1),role+' invalid slot')
            require(r['readback_bytes']==(0 if role=='relay' else size) and r['converted_bytes']==(size if role=='source' else 0),role+' copy/conversion accounting')
            if role!='source':require(all(r[k]==0 for k in source_fields),role+' fabricated source observation')
            if role!='receiver':require(all(r[k]==0 for k in final_fields),role+' unexpected final completion')
            if role!='relay':require(all(r[k]==0 for k in relay_fields),role+' unexpected relay observation')
            if role=='relay':
                require(r['digest']=='-' and r['native_matches']==1,'relay payload observation or identity match failure')
                require(0<r['arrival_ns']<=r['forward_begin_ns']<=r['forwarded_ns'],'relay forward phase order')
            else:require(re.fullmatch('[0-9a-f]{64}',r['digest']) is not None,role+' invalid digest')
            if role=='source':
                require(r['decoded_dts_ns']==2**64-1 and r['decoded_pts_ns']-r['decoded_origin_ns']==i*10**9//fps,'actual decoder PTS/DTS')
                order=('scheduled_ns','acquire_begin_ns','acquired_ns','decode_begin_ns','decoded_ns','convert_begin_ns','converted_ns','conversion_done_ns','oracle_begin_ns','oracle_done_ns','submit_begin_ns','submitted_ns')
                require(0<r[order[0]] and all(r[b]>=r[a] for a,b in zip(order,order[1:])),'source phase order')
            if role=='receiver':
                order=('arrival_ns','copy_begin_ns','signalled_ns','gpu_done_ns','lease_done_ns','hash_begin_ns','hash_done_ns')
                require(0<r[order[0]] and all(r[b]>=r[a] for a,b in zip(order,order[1:])),'receiver completion phase order')
                require(r['lease']>0 and r['completion_value']==i+1,'missing real lease/fence completion')
        if role!='source':
            require(all(b['sequence']>a['sequence'] and b['ready_value']>a['ready_value'] for a,b in zip(v,v[1:])),'native sequence/fence replay')
            require(all(r['sequence']>0 and r['ready_value']>0 for r in v),'missing native sequence/fence')
        if role=='receiver':require(len({r['lease'] for r in v})==n,'duplicate lease')
        for i,r in enumerate(a):
            require(r['pts_ns']==i*10000000 and r['samples']==480 and r['bytes']==1920,role+' PCM cadence/count')
            require(r['observed_pts_ns']==r['mapped_pts_ns'],role+' PCM actual mapping')
        offset=a[0]['observed_pts_ns']-a[0]['pts_ns']
        require(all(r['observed_pts_ns']-r['pts_ns']==offset for r in a) and all(r['mapped_pts_ns']-r['pts_ns']==offset for r in v),role+' common A/V mapping')
        s=json.loads((root/(role+'.summary.json')).read_text())
        require(s['video']==n and s['audio_blocks']==audio and s['pcm_frames']==seconds*48000 and s['terminal'] is True,role+' terminal counts')
        if role!='source':require(s['held_last_proof'] is True,role+' missing final hold proof')
        if role=='relay':require(s['same_allocation_checked'] is True,'missing native allocation check')
        with (root/(role+'.resources.csv')).open() as f:resources=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(f)]
        result['resources'][role]=resource_analysis(resources,seconds)
    source=video['source'];relay=video['relay'];final=video['receiver'];epoch=source[0]['scheduled_ns'];origin=source[0]['decoded_origin_ns'];generation=source[0]['generation'];late=[];pairs={}
    require(len({r['digest'] for r in source})==n,'snow source digests must be distinct for every expected frame')
    for i,(s,m,r) in enumerate(zip(source,relay,final)):
        require(s['digest']==r['digest'],'independent source/final full-pixel digest mismatch')
        require(s['generation']==m['generation']==r['generation']==generation,'changed generation')
        require(s['decoded_origin_ns']==origin and s['scheduled_ns']==epoch+i*10**9//fps,'source epoch/cadence changed')
        # Frame visibility may precede submit/forward function return.
        require(s['submit_begin_ns']<=m['arrival_ns']<=r['arrival_ns'] and m['forward_begin_ns']<=r['arrival_ns'],'native causal order')
        require(s['slot']==m['slot'],'source/upstream slot changed')
        pairs.setdefault(m['slot'],set()).add(r['slot'])
        if r['arrival_ns']-s['scheduled_ns']>budget:late.append(i)
    require(all(len(v)==1 for v in pairs.values()) and len({next(iter(v)) for v in pairs.values()})==len(pairs),'non-bijective allocation slot correspondence')
    audio_late=[]
    for i,(s,m,r) in enumerate(zip(pcm['source'],pcm['relay'],pcm['receiver'])):
        require(s['scheduled_ns']==epoch+i*10000000 and s['scheduled_ns']==m['scheduled_ns']==r['scheduled_ns'],'PCM epoch/cadence')
        require(s['source_ns']==m['source_ns']==r['source_ns'] and s['scheduled_ns']<=s['source_ns']<=m['arrival_ns']<=r['arrival_ns'],'PCM source identity/causality')
        if r['arrival_ns']-s['scheduled_ns']>budget:audio_late.append(i)
    result.update(video_frames=n,pcm_frames=seconds*48000,source_distinct_digests=len({r['digest'] for r in source}),video_arrival_misses=late,audio_arrival_misses=audio_late,source_conversion_bytes=n*size,source_readback_bytes=n*size,final_readback_bytes=n*size)
    result['timing_ms']={'scheduled_to_final_arrival':distribution([r['arrival_ns']-s['scheduled_ns'] for s,r in zip(source,final)]),'audio_scheduled_to_final':distribution([r['arrival_ns']-s['scheduled_ns'] for s,r in zip(pcm['source'],pcm['receiver'])])}
    for name,rows,a,b in [('source_acquire',source,'acquire_begin_ns','acquired_ns'),('decoder_pull_wait',source,'decode_begin_ns','decoded_ns'),('conversion_call',source,'convert_begin_ns','converted_ns'),('conversion_until_gpu_observed_done',source,'convert_begin_ns','conversion_done_ns'),('source_oracle_readback_and_hash',source,'oracle_begin_ns','oracle_done_ns'),('source_submit',source,'submit_begin_ns','submitted_ns'),('relay_forward',relay,'forward_begin_ns','forwarded_ns'),('final_acquire_until_lease_done',final,'arrival_ns','lease_done_ns'),('final_staging_map_and_hash',final,'hash_begin_ns','hash_done_ns')]:result['timing_ms'][name]=distribution([r[b]-r[a] for r in rows])
    result['trace_checked']=False
    if c['trace']:
        result['trace_counts']=trace_analysis(root,c,video);result['trace_checked']=True
    result['trace_scope']='FrameLink transport instrumentation only; uninstrumented GStreamer/D3D conversion and oracle copies are not inferred absent'
    result['pass']=not late and not audio_late
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory');a=p.parse_args()
    try:r=analyze(a.directory)
    except (ValueError,KeyError,TypeError,OSError,ZeroDivisionError) as e:r={'pass':False,'error':str(e)}
    print(json.dumps(r,indent=2));return 0 if r['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
