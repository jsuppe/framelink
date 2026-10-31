#!/usr/bin/env python3
"""Timestamped AV scheduling telemetry, preview disabled, native offscreen output."""
import os,pathlib,json,subprocess,time,urllib.request,socket
root=pathlib.Path('C:/Users/jonsu/dev/m5-render-decoded');root.mkdir(exist_ok=True)
b=pathlib.Path('C:/Users/jonsu/dev/framelink-m5-pipeline/build/Release');renderer=pathlib.Path('C:/Users/jonsu/dev/vkrender-m5-pipeline/build/Release/vkrenderer.exe')
baseenv=dict(os.environ);baseenv['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+baseenv['PATH']
summary=[]
for width,height,fps in [(160,90,50)]:
 for fmt in ['BGRA','NV12']:
  for mode in ['av']:
   name=f'{width}x{height}-{fps}-{fmt}-{mode}';d=root/name;d.mkdir(exist_ok=True);n=fps*10
   with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
   env=dict(baseenv,VKR_TRACE=str(d/'renderer'),FRAMELINK_TRACE=str(d/'transport'),FRAMELINK_GST_TRACE=str(d/'gst'))
   rc=[str(renderer),'--serve',name,'--headless',str(-1),'--control-port',str(port)]
   if mode=='av':
    rc+=['--av-exit-on-end','--av-session',str(d/'session'),'--av-plugin',str(b/'gstframelink.dll'),'--av-format',fmt,'--av-width',str(width),'--av-height',str(height),'--av-fps',str(fps),'--av-audio-sink','autoaudiosink']
    pipeline=d/'source.pipeline';pipeline.write_text('filesrc location="C:/Users/jonsu/dev/framelink-m5-pipeline/visible-demo/demo-10s.avi" ! avidemux name=d d.video_0 ! queue ! jpegdec ! videoconvert ! @VIDEO_SINK@ d.audio_0 ! queue ! audioconvert ! @AUDIO_SINK@')
    pc=[str(b/'framelink_av_producer.exe'),str(b/'gstframelink.dll'),'--session',str(d/'session'),'--format',fmt,'--frames',str(n),'--width',str(width),'--height',str(height),'--fps',str(fps),'--input-pipeline',str(pipeline),'--timeout-ms','15000']
   else:
    env['GST_PLUGIN_PATH']=str(b);env['VKR_INPUT_FORMATS']=fmt.lower()
    pc=['gst-launch-1.0.exe','-q','videotestsrc','is-live=true','num-buffers='+str(n),'pattern=ball','!','video/x-raw,format='+fmt+f',width={width},height={height},framerate={fps}/1','!','framelinksink','channel='+name+'.in.0','sync=true']
   samples=[];start=time.perf_counter()
   with (d/'producer.log').open('w') as po,(d/'renderer.log').open('w') as ro:
    r=subprocess.Popen(rc,stdout=ro,stderr=subprocess.STDOUT,env=env);time.sleep(.3);p=subprocess.Popen(pc,stdout=po,stderr=subprocess.STDOUT,env=env)
    try:
     while time.perf_counter()-start<18 and (p.poll() is None or (mode=='av' and r.poll() is None)):
      try:
       with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/status',timeout=.2) as x: samples.append({'t':time.perf_counter()-start,'status':json.load(x)})
      except Exception:pass
      time.sleep(.05)
     p.wait(timeout=1)
     if mode=='av':r.wait(timeout=1)
    except subprocess.TimeoutExpired:pass
    finally:
     # Graceful renderer shutdown saves trace: /api/quit is not exposed. Video mode uses finite headless timeout instead below.
     for q in [p,r]:
      if q.poll() is None:q.terminate();q.wait()
   row={'case':name,'wall_s':time.perf_counter()-start,'exit_codes':[p.returncode,r.returncode],'commands':[pc,rc]};summary.append(row);(d/'status.json').write_text(json.dumps(samples,indent=2));(root/'summary.json').write_text(json.dumps(summary,indent=2));print(row,flush=True)
