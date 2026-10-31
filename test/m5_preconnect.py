#!/usr/bin/env python3
"""Producer import must finish before shared epoch, not at first media render."""
import argparse,csv,json,os,pathlib,subprocess,sys,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--output');a=p.parse_args()
b=pathlib.Path(a.build).resolve();root=pathlib.Path(a.output or tempfile.mkdtemp(prefix='m5-preconnect-')).resolve();root.mkdir(parents=True,exist_ok=True)
results=[]
for mode in ('ready','legacy'):
    out=root/mode;out.mkdir(exist_ok=True)
    env=dict(os.environ,FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS='350',FRAMELINK_GST_TRACE=str(out/'gst'))
    env.pop('FRAMELINK_TEST_LAZY_PRODUCER_OPEN',None)
    if mode=='legacy':env['FRAMELINK_TEST_LAZY_PRODUCER_OPEN']='1'
    run=subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('m5_external_pipeline.py')),'--build',str(b),'--output',str(out),'--clock-policy','producer'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
    (out/'run.log').write_text(run.stdout)
    assert run.returncode==0,(mode,run.stdout)
    for fmt in ('BGRA','NV12'):
        records=[json.loads(line) for line in (out/(fmt+'-consumer.log')).read_text().splitlines() if line.startswith('{')]
        summary=next(r for r in records if r.get('event')=='video-schedule-summary')
        clock=next(r for r in records if r.get('event')=='playback-clock')
        assert clock['latency_ns']==100_000_000,clock
        received=[r['pts_ns'] for r in records if r.get('event')=='video-schedule' and r.get('stage')=='received']
        if mode=='ready':assert summary['received']==summary['scheduled']==100 and received==list(range(0,2_000_000_000,20_000_000)),summary
        else:assert summary['received']<100 or summary['late']>0,('legacy negative control did not fail',summary)
        results.append(dict(mode=mode,format=fmt,**summary))
    traces=[]
    for path in out.glob('gst.*.csv'):
        rows=list(csv.DictReader(line for line in path.read_text().splitlines() if not line.startswith('#')))
        media=[r for r in rows if r['operation']=='gst.frame_receive']
        if not media:continue
        opens=[r for r in rows if r['operation']=='gst.producer_open']
        assert opens and int(opens[0]['duration_ns'])>=350_000_000,opens
        first=min(int(r['start_ns']) for r in media)
        if mode=='ready':
            ready=[r for r in rows if r['operation']=='gst.producer_preconnect']
            assert len(ready)==1 and int(ready[0]['start_ns'])+int(ready[0]['duration_ns'])<first,ready
        else:assert int(opens[0]['start_ns'])>=first,opens
        traces.append(path.name)
    assert len(traces)==2,traces
# Both process launch orders use the same authenticated readiness path.
for first in ('producer','consumer'):
    out=root/('order-'+first);out.mkdir(exist_ok=True)
    ext='.exe' if os.name=='nt' else ''
    plugin=b/('gstframelink.dll' if os.name=='nt' else 'libgstframelink.so')
    common=[str(plugin),'--session',str(out/'session'),'--frames','100','--timeout-ms','8000','--trace-media']
    commands={'producer':[str(b/('framelink_av_producer'+ext)),*common],
              'consumer':[str(b/('framelink_av_consumer'+ext)),*common,'--stream','--clock-policy','producer','--audio-sink','fakesink']}
    children=[];files=[]
    try:
        for role in (first,'consumer' if first=='producer' else 'producer'):
            log=(out/(role+'.log')).open('w');files.append(log)
            children.append(subprocess.Popen(commands[role],env=dict(os.environ,FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS='350'),stdout=log,stderr=subprocess.STDOUT))
            time.sleep(.15)
        assert all(child.wait(timeout=15)==0 for child in children),first
    finally:
        for child in children:
            if child.poll() is None:child.kill();child.wait(timeout=2)
        for log in files:log.close()
    records=[json.loads(line) for line in (out/'consumer.log').read_text().splitlines() if line.startswith('{')]
    summary=next(r for r in records if r.get('event')=='video-schedule-summary')
    assert summary['received']==summary['scheduled']==100,summary
    results.append(dict(mode='order-'+first,**summary))
# Missing consumer cannot leave start waiting indefinitely; no injection here.
env=dict(os.environ,GST_PLUGIN_PATH=str(b)+os.pathsep+os.environ.get('GST_PLUGIN_PATH',''))
for key in ('FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS','FRAMELINK_TEST_LAZY_PRODUCER_OPEN'):env.pop(key,None)
start=time.monotonic()
run=subprocess.run(['gst-launch-1.0'+('.exe' if os.name=='nt' else ''),'-q','videotestsrc','num-buffers=1','!','video/x-raw,format=BGRA,width=160,height=90','!','framelinksink','channel=preconnect-missing-'+str(os.getpid()),'connect-timeout-ms=100'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=5)
assert run.returncode!=0 and 'readiness deadline' in run.stdout,run.stdout
assert time.monotonic()-start<3,'connection deadline did not bound absent-consumer retry'
(root/'deadline.log').write_text(run.stdout)
(root/'summary.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
