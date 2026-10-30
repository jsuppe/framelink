#!/usr/bin/env python3
"""Paced source drift: exact media PTS, bounded queues and real recovery.

Fakesink exercises clock scheduling/backpressure, NOT physical device resampling.
"""
import argparse,json,pathlib,tempfile,time,subprocess,os
from m3_apps import Peer
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--output');a=p.parse_args()
b=pathlib.Path(a.build).resolve();ext='.exe' if os.name=='nt' else ''
root=pathlib.Path(a.output or tempfile.mkdtemp(prefix='fl-source-drift-'));root.mkdir(parents=True,exist_ok=True)
plugin=str(b/('gstframelink.dll' if os.name=='nt' else 'libgstframelink.so'))
for ppm in (-1000,1000,100000,-100000):
    d=root/str(ppm);d.mkdir();peers=[]
    common=[plugin,'--session',str(d/'session'),'--frames','300','--timeout-ms','12000','--fps','50']
    try:
        peers.append(Peer([str(b/('framelink_av_producer'+ext))]+common+['--pacing-ppm',str(ppm),'--audio-offset-ns','37000000'],d,'producer'))
        peers.append(Peer([str(b/('framelink_av_consumer'+ext))]+common+['--stream','--audio-sink','fakesink'],d,'consumer'))
        end=time.monotonic()+20
        for peer in peers:peer.finish(end)
        tx,rx=peers
        metrics=[e for e in rx.events if e.get('event')=='playback-metrics']
        assert metrics,rx.lines
        m=metrics[-1]
        assert m['peak_queue_buffers']<=4,m
        # Queue admits a last buffer crossing its time threshold, at most 20ms.
        assert m['peak_queue_ns']<=100000000,m
        if ppm==-100000:
            assert rx.process.returncode!=0,rx.lines
            assert m['late_audio_blocks']>=3,m
            assert any('playout budget exhausted' in l for l in rx.lines),rx.lines
        else:
            assert all(x.process.returncode==0 for x in peers),[x.lines for x in peers]
            t=[e for e in rx.events if e.get('event')=='playback-timing'][-1]
            assert t['audio_end_ns']-t['video_end_ns']==37000000,t
            c=[e for e in rx.events if e.get('event')=='playback-complete'][-1]
            assert c['audio_samples']==288000 and c['audio_blocks']==300,c
            if ppm==100000:
                pacing=[e for e in tx.events if e.get('event')=='producer-pacing'][-1]
                assert pacing['pacing_ppm']==ppm,pacing
                assert m['queue_full_events']>0,m
        (d/'result.json').write_text(json.dumps(m,indent=2)+'\n')
        print(ppm,'PASS',m,flush=True)
    finally:
        for peer in peers:peer.stop()
# Exercise production runPlayback automatic coordinated re-anchor, not just CLI
# detection: replace the intentionally slow clock with a healthy producer.
d=root/'recovery';d.mkdir();session=str(d/'session');procs=[]
def launch(name,cmd):
    f=open(d/(name+'.log'),'w');q=subprocess.Popen(cmd,stdout=f,stderr=f);procs.append((q,f));return q
try:
    producer=str(b/('framelink_av_producer'+ext))
    bad=launch('slow',[producer,plugin,'--session',session,'--frames','300','--pacing-ppm','-100000','--timeout-ms','12000'])
    consumer=launch('consumer',[str(b/('fl_playback_recovery'+ext)),plugin,session,'recover'])
    deadline=time.monotonic()+12
    while 'playout budget exhausted' not in (d/'consumer.log').read_text():
        assert consumer.poll() is None,(d/'consumer.log').read_text()
        assert time.monotonic()<deadline,'no bounded exhaustion recovery'
        time.sleep(.02)
    bad.terminate();bad.wait(timeout=5)
    good=launch('healthy',[producer,plugin,'--session',session,'--frames','75','--timeout-ms','12000'])
    assert consumer.wait(timeout=15)==0,(d/'consumer.log').read_text()
    assert good.wait(timeout=5)==0,(d/'healthy.log').read_text()
    print('coordinated source-drift recovery PASS',flush=True)
finally:
    for q,f in procs:
        if q.poll() is None:q.terminate()
        q.wait(timeout=5);f.close()
print('PASS: 5 source pacing/recovery scenarios; evidence='+str(root))
