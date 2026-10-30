#!/usr/bin/env python3
"""Finite external decoded A/V integration. Requires GStreamer AVI plugins + GPU.
Hashes trace-mode media; this is a correctness test, not a throughput benchmark.
"""
import argparse,json,os,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--output');a=p.parse_args()
b=pathlib.Path(a.build).resolve();root=pathlib.Path(a.output or tempfile.mkdtemp(prefix='fl-m5-')).resolve();root.mkdir(parents=True,exist_ok=True)
exe='.exe' if os.name=='nt' else '';plugin=b/('gstframelink.dll' if os.name=='nt' else 'libgstframelink.so')
media=root/'decoded-source.avi'
# Mux ordinary MJPEG/PCM AVI; consumer never sees test-source elements.
cmd=['gst-launch-1.0'+exe,'-q','videotestsrc','num-buffers=100','pattern=ball','!','video/x-raw,format=I420,width=160,height=90,framerate=50/1','!','jpegenc','!','queue','!','mux.','audiotestsrc','num-buffers=100','samplesperbuffer=960','wave=sine','freq=440','!','audio/x-raw,format=S16LE,rate=48000,channels=2','!','queue','!','mux.','avimux','name=mux','!','filesink','location='+media.as_posix()]
subprocess.run(cmd,check=True,timeout=30)
results=[]
def records(path):
    return [json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')]
for fmt in ('BGRA','NV12'):
    pipeline=root/(fmt+'.pipeline')
    pipeline.write_text(f'filesrc location="{media.as_posix()}" ! avidemux name=demux demux.video_0 ! queue ! jpegdec ! videoconvert ! @VIDEO_SINK@ demux.audio_0 ! queue ! audioconvert ! @AUDIO_SINK@\n')
    args=[str(plugin),'--session',str(root/('session-'+fmt)),'--format',fmt,'--frames','100','--timeout-ms','15000','--trace-media']
    plog=root/(fmt+'-producer.log');clog=root/(fmt+'-consumer.log')
    with plog.open('w') as pf,clog.open('w') as cf:
        producer=subprocess.Popen([str(b/('framelink_av_producer'+exe)),*args,'--input-pipeline',str(pipeline)],stdout=pf,stderr=subprocess.STDOUT)
        consumer=subprocess.Popen([str(b/('framelink_av_consumer'+exe)),*args,'--stream','--audio-sink','fakesink'],stdout=cf,stderr=subprocess.STDOUT)
        try:
            codes=(producer.wait(timeout=25),consumer.wait(timeout=25))
        finally:
            for child in (producer,consumer):
                if child.poll() is None:child.kill();child.wait()
    assert codes==(0,0),(fmt,codes,str(root))
    sent=[r for r in records(plog) if r.get('event')=='media-trace'];received=[r for r in records(clog) if r.get('event')=='media-trace']
    def keyed(rows,track):return {r['pts_ns']:(r['sha256'],r['bytes'],r['duration_ns']) for r in rows if r['track']==track}
    sa,ra=keyed(sent,'audio'),keyed(received,'audio');sv,rv=keyed(sent,'video'),keyed(received,'video')
    assert len(sa)==100 and sa==ra,'PCM content/PTS/duration changed'
    assert len(sv)==100 and len(rv)>=2 and max(rv)==1980000000,(len(sv),len(rv))
    assert all(sv.get(pts)==value for pts,value in rv.items()),'video content/PTS/duration changed'
    assert len({value[0] for value in rv.values()})>1,'no changing video content'
    complete=next(r for r in records(clog) if r.get('event')=='playback-complete')
    assert complete['audio_samples']==96000
    assert complete['video_late']<10,complete
    results.append({'format':fmt,'producer_video_frames':len(sv),'sink_arrival_video_frames':len(rv),'scheduled_video_frames':complete['video_received'],'matched_audio_blocks':len(ra),'audio_samples':complete['audio_samples'],'late_video':complete['video_late'],'payload_pts_duration_preserved':True,'physical_playback_qualified':False})
(root/'summary.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2));print(root)
