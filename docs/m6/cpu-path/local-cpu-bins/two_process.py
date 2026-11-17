from pathlib import Path
import os,subprocess,json,hashlib,time
root=Path(__file__).parent/'two-process';root.mkdir(exist_ok=True)
env=dict(os.environ,GST_PLUGIN_PATH='<linux-home>/dev/build/framelink-m6-linux',LD_LIBRARY_PATH='<linux-home>/dev/build/framelink-m6-linux')
results=[]
for fmt,size in [('BGRA',64*64*4),('NV12',64*64*3//2)]:
 case=root/fmt;case.mkdir(exist_ok=True);session=case/'session'
 common=['name=t','shared-cpu=true','session='+str(session),'video-format='+fmt,'width=64','height=64','framerate=60/1','video-queue-frames=1','video-pool=2','connect-timeout-ms=3000']
 rx=['gst-launch-1.0','-q','framelinkavsrc']+common+['t.video_src','!','filesink','async=false','sync=false','location='+str(case/'video.out'),'t.audio_src','!','filesink','async=false','sync=false','location='+str(case/'audio.out')]
 tx=['gst-launch-1.0','-q','framelinkavsink']+common+['videotestsrc','is-live=true','num-buffers=32','!','video/x-raw,format='+fmt+',width=64,height=64,framerate=60/1','!','tee','name=v','v.','!','queue','!','t.video_sink','v.','!','queue','!','filesink','sync=false','location='+str(case/'video.ref'),'audiotestsrc','is-live=true','num-buffers=54','samplesperbuffer=480','!','audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2','!','tee','name=a','a.','!','queue','!','t.audio_sink','a.','!','queue','!','filesink','sync=false','location='+str(case/'audio.ref')]
 (case/'commands.json').write_text(json.dumps({'producer':tx,'consumer':rx},indent=2))
 with (case/'rx.log').open('w') as ro,(case/'tx.log').open('w') as to:
  consumer=subprocess.Popen(rx,env=env,stdout=ro,stderr=subprocess.STDOUT)
  try:
   producer=subprocess.run(tx,env=env,stdout=to,stderr=subprocess.STDOUT,timeout=12)
   rc=consumer.wait(timeout=12)
  finally:
   if consumer.poll() is None:consumer.kill();consumer.wait()
 assert producer.returncode==0 and rc==0,(fmt,producer.returncode,rc)
 record={'format':fmt,'producer_exit':producer.returncode,'consumer_exit':rc,'video_frames':32,'audio_frames':54*480}
 for stream,expected in [('video',32*size),('audio',54*480*4)]:
  reference=(case/(stream+'.ref')).read_bytes();actual=(case/(stream+'.out')).read_bytes()
  assert len(reference)==expected and actual==reference,(fmt,stream,len(reference),len(actual),expected)
  record[stream+'_bytes']=expected;record[stream+'_sha256']=hashlib.sha256(actual).hexdigest()
 results.append(record);(root/'results.json').write_text(json.dumps(results,indent=2));print(record,flush=True)
