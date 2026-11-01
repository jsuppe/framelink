#!/usr/bin/env python3
"""Windows external decode -> independent native renderer, bounded isolated runs.
Offscreen rendering and selected audio device clock, NOT optical/acoustic sync.
"""
import argparse,hashlib,json,os,pathlib,socket,subprocess,time,urllib.request,urllib.error
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--renderer',required=True);p.add_argument('--output',required=True);a=p.parse_args()
b=pathlib.Path(a.build).resolve();root=pathlib.Path(a.output).resolve();root.mkdir(parents=True,exist_ok=True)
renderer=pathlib.Path(a.renderer).resolve();plugin=b/'gstframelink.dll';media=root/'renderer-input.avi'
subprocess.run(['gst-launch-1.0.exe','-q','videotestsrc','num-buffers=300','pattern=ball','!','video/x-raw,format=I420,width=160,height=90,framerate=50/1','!','jpegenc','!','queue','!','mux.','audiotestsrc','num-buffers=300','samplesperbuffer=960','wave=sine','freq=440','volume=0.02','!','audio/x-raw,format=S16LE,rate=48000,channels=2','!','queue','!','mux.','avimux','name=mux','!','filesink','location='+media.as_posix()],check=True,timeout=30)
summary=[]
for name,fmt,fault in [('bgra','BGRA',''),('nv12','NV12',''),('source-death','BGRA','kill'),('missing-input','BGRA','missing')]:
    d=root/name;d.mkdir(exist_ok=True)
    path=media if fault!='missing' else root/'does-not-exist.avi'
    pipeline=d/'input.pipeline';pipeline.write_text(f'filesrc location="{path.as_posix()}" ! avidemux name=d d.video_0 ! queue ! jpegdec ! videoconvert ! @VIDEO_SINK@ d.audio_0 ! queue ! audioconvert ! @AUDIO_SINK@\n')
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    url=f'http://127.0.0.1:{port}';env=dict(os.environ,GST_DEBUG='autoaudiosink:5,wasapi*:4');samples=[];snapshots=[];killed=False;start=time.monotonic()
    def get(endpoint):
        with urllib.request.urlopen(url+endpoint,timeout=1) as r:return r.read()
    with (d/'producer.log').open('w') as pl,(d/'renderer.log').open('w') as rl:
        producer=subprocess.Popen([str(b/'framelink_av_producer.exe'),str(plugin),'--session',str(d/'session'),'--format',fmt,'--frames','300','--input-pipeline',str(pipeline),'--timeout-ms','15000'],stdout=pl,stderr=subprocess.STDOUT)
        render=subprocess.Popen([str(renderer),'--serve','m5-'+name,'--headless','-1','--av-exit-on-end','--control-port',str(port),'--av-session',str(d/'session'),'--av-plugin',str(plugin),'--av-format',fmt,'--av-audio-sink','autoaudiosink'],stdout=rl,stderr=subprocess.STDOUT,env=env,cwd=d)
        try:
            # Receiver defaults: 15s per attempt, initial + 3 recovery attempts.
            while time.monotonic()-start<(70 if fault else 23) and render.poll() is None:
                time.sleep(.1)
                try:sample=json.loads(get('/api/status'))
                except (OSError,ValueError):continue
                samples.append(sample);av=sample.get('av',{})
                if fault=='kill' and av.get('due_decisions',0)>10 and not killed:producer.kill();killed=True
                if not fault and av.get('due_decisions',0)>20 and len(snapshots)<2:
                    try:
                        data=get('/api/preview.jpg')
                        if data[:2]==b'\xff\xd8':
                            digest=hashlib.sha256(data).hexdigest()
                            if not snapshots or snapshots[-1]!=digest:
                                snapshot=d/f'preview-{len(snapshots)}.jpg';snapshot.write_bytes(data);snapshots.append(digest)
                            time.sleep(.4)
                    except OSError:pass
            render.wait(timeout=3);producer.wait(timeout=3)
            codes=[producer.returncode,render.returncode]
        finally:
            for proc in (producer,render):
                if proc.poll() is None:proc.kill();proc.wait()
            (d/'status.json').write_text(json.dumps(samples,indent=2))
    assert samples,(name,'no renderer status')
    avs=[x['av'] for x in samples if 'av' in x]
    assert all(x['invalid_pts']==0 and x['max_queue_depth']<=6 and x['import_failures']==0 for x in avs)
    if not fault:
        assert codes==[0,0],(name,codes)
        assert max(x['due_decisions'] for x in avs)>20
        assert any(x['audio_master'] and x['audio_samples']>48000 for x in avs),'real audio clock not selected'
        assert len(snapshots)==2 and snapshots[0]!=snapshots[1],(name,'preview not changing')
    else:
        assert codes[0]!=0,(name,'producer failure not detected')
        if fault=='kill':assert killed
    # Exited renderer must relinquish its control listener; do not merely kill it.
    try:get('/api/status');raise AssertionError('control port still alive')
    except OSError:pass
    summary.append({'case':name,'exit_codes':codes,'elapsed_s':round(time.monotonic()-start,3),'max_due_decisions':max(x['due_decisions'] for x in avs),'audio_master_observed':any(x['audio_master'] for x in avs),'preview_sha256':snapshots,'bounded_process_exit':True,'control_port_closed':True,'physical_sync_measured':False})
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
(root/'binary-hashes.json').write_text(json.dumps({str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in (renderer,b/'framelink_av_producer.exe',plugin)},indent=2))
