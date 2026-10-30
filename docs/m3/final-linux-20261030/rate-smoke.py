import pathlib,subprocess,tempfile,json
out=pathlib.Path('/tmp/fl-m3-rate-evidence');out.mkdir(exist_ok=True)
for rate,ch,fps in [(44100,1,59),(48000,2,60)]:
 root=tempfile.mkdtemp(prefix='fl-m3-rate-');common=['/tmp/framelink-audio-build/libgstframelink.so','--session',root+'/session','--frames','60','--format','NV12','--rate',str(rate),'--channels',str(ch),'--fps',str(fps),'--timeout-ms','15000'];tag=f'{rate}-{ch}-{fps}'
 with (out/(tag+'-consumer.log')).open('w') as c,(out/(tag+'-producer.log')).open('w') as p:
  consumer=subprocess.Popen(['/tmp/framelink-audio-build/framelink_av_consumer',*common],stdout=c,stderr=subprocess.STDOUT)
  producer=subprocess.Popen(['/tmp/framelink-audio-build/framelink_av_producer',*common],stdout=p,stderr=subprocess.STDOUT)
  try:
   assert producer.wait(timeout=20)==0
   assert consumer.wait(timeout=20)==0
  finally:
   for proc in [producer,consumer]:
    if proc.poll() is None:proc.kill();proc.wait()
 events=[json.loads(x) for x in (out/(tag+'-consumer.log')).read_text().splitlines() if x.startswith('{')];assert any(e.get('ok') is True and e.get('video_frames')==60 and e.get('audio_blocks')==60 for e in events),events
 print(tag,'60 verified pairs passed',flush=True)
