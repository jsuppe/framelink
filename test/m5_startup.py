#!/usr/bin/env python3
"""Delayed device setup must not turn startup into a burst of dropped frames.
Runs decoded BGRA/NV12 integration with 700ms injected device-preroll delay.
The legacy uncoordinated audio policy is the required negative control.
This tests startup transport completeness, NOT display FPS or physical A/V sync.
"""
import argparse,json,os,pathlib,subprocess,sys,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--output');a=p.parse_args()
root=pathlib.Path(a.output or tempfile.mkdtemp(prefix='m5-startup-')).resolve();root.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,FRAMELINK_TEST_PREROLL_DELAY_MS='700')
results=[]
for policy in ('producer','audio'):
    out=root/policy
    run=subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('m5_external_pipeline.py')),'--build',a.build,'--output',str(out),'--clock-policy',policy],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
    (root/(policy+'.log')).write_text(run.stdout)
    cases=[]
    for fmt in ('BGRA','NV12'):
        path=out/(fmt+'-producer.log')
        if not path.exists():continue
        rows=[json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')]
        stats=[r for r in rows if r.get('event')=='video-transport-summary']
        if not stats:continue
        received=[json.loads(s) for s in (out/(fmt+'-consumer.log')).read_text().splitlines() if s.startswith('{')]
        video=[r['pts_ns'] for r in received if r.get('event')=='media-trace' and r.get('track')=='video']
        intact=stats[-1]['submitted']==100 and stats[-1]['ring_full_drops']==0 and len(set(video))==100 and min(video)==0
        cases.append(dict(format=fmt,intact=intact,stats=stats[-1],received=len(set(video))))
    if policy=='producer':assert run.returncode==0 and len(cases)==2 and all(c['intact'] for c in cases),(run.stdout,cases)
    else:assert cases and any(not c['intact'] for c in cases),('negative control failed to detect startup loss',cases)
    results.append(dict(policy=policy,returncode=run.returncode,cases=cases))
# Peer death while the producer is gated must not strand a streaming thread.
b=pathlib.Path(a.build).resolve();ext='.exe' if os.name=='nt' else ''
plugin=b/('gstframelink.dll' if os.name=='nt' else 'libgstframelink.so')
common=[str(plugin),'--session',str(root/'restart-session'),'--frames','40','--timeout-ms','4000']
for attempt in ('cancel','restart','long-preroll'):
    children=[]
    case_common=list(common)
    if attempt=='long-preroll':case_common[-1]='6000'
    delay='2300' if attempt=='long-preroll' else '1500'
    started=time.monotonic()
    with (root/(attempt+'-producer.log')).open('w') as plog,(root/(attempt+'-consumer.log')).open('w') as clog:
        try:
            producer=subprocess.Popen([str(b/('framelink_av_producer'+ext)),*case_common],stdout=plog,stderr=subprocess.STDOUT);children.append(producer)
            consumer=subprocess.Popen([str(b/('framelink_av_consumer'+ext)),*case_common,'--stream','--clock-policy','producer','--audio-sink','fakesink'],env=dict(os.environ,FRAMELINK_TEST_PREROLL_DELAY_MS=delay),stdout=clog,stderr=subprocess.STDOUT);children.append(consumer)
            if attempt=='cancel':
                time.sleep(.75);consumer.kill();consumer.wait(timeout=2)
            codes=(producer.wait(timeout=8),consumer.wait(timeout=8))
            assert (codes[0]!=0 if attempt=='cancel' else codes==(0,0)),(attempt,codes)
            if attempt!='cancel':
                clog.flush()
                rows=[json.loads(line) for line in (root/(attempt+'-consumer.log')).read_text().splitlines() if line.startswith('{')]
                complete=next(r for r in rows if r.get('event')=='playback-complete')
                assert complete['video_received']==40 and complete['audio_samples']==38400,complete
                if attempt=='long-preroll':
                    delayed=next(r for r in rows if r.get('event')=='startup-preroll-delay')
                    assert delayed['elapsed_us']>2_000_000,delayed
            results.append(dict(case=attempt,exit_codes=codes,elapsed_seconds=time.monotonic()-started))
        finally:
            for child in children:
                if child.poll() is None:child.kill();child.wait(timeout=2)
(root/'summary.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2));print(root)
