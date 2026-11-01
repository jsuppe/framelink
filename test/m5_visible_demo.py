#!/usr/bin/env python3
"""Finite external-file A/V demo, isolated ports; Windows interactive task friendly."""
import argparse, json, os, pathlib, socket, subprocess, threading, time, urllib.request, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
p=argparse.ArgumentParser();p.add_argument('--browser-preview',action='store_true',help='Use low-rate diagnostic JPEG preview instead of native window');p.add_argument('--build',required=True);p.add_argument('--renderer',required=True);p.add_argument('--output',required=True);p.add_argument('--port',type=int,default=18765);p.add_argument('--renderer-port',type=int,default=18766);p.add_argument('--loops',type=int,default=60);a=p.parse_args()
if not (1 <= a.loops <= 100 and 1024 <= a.port <= 65535 and 1024 <= a.renderer_port <= 65535 and a.port != a.renderer_port):p.error('require 1..100 loops and two distinct ports1024..65535')
with socket.socket() as probe:probe.bind(('127.0.0.1',a.renderer_port))
b=pathlib.Path(a.build).resolve();r=pathlib.Path(a.output).resolve();r.mkdir(parents=True,exist_ok=True)
child_flags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)
stop=threading.Event();state={'phase':'preparing','physical_sync_measured':False,'video_output':'browser-jpeg' if a.browser_preview else 'native-vulkan-window'};children=[]
html=b'''<!doctype html><title>FrameLink M5 - Windows external A/V demo</title><style>body{background:#171923;color:white;font:20px sans-serif;margin:25px}img{width:90vw;max-height:75vh;object-fit:contain}pre{font-size:14px}</style><h2>FrameLink M5: decoded file -> independent renderer + WASAPI audio</h2><p>Quiet 440 Hz tone. BGRA transport. Video defaults to a separate native Vulkan window. Browser JPEG is optional diagnostics only; neither submission nor JPEG counters prove physical scanout.</p><img id="v"><pre id="s"></pre><button onclick="fetch('/stop',{method:'POST'})">Stop this demo</button><script>const browserPreview=PREVIEW_ENABLED;let loaded=0,started=performance.now();async function status(){try{let r=await fetch('/status');let data=await r.json();data.browser_jpeg={loaded:loaded,loads_per_second:loaded*1000/(performance.now()-started),physical_scanout_measured:false};s.textContent=JSON.stringify(data,null,2)}catch(e){}setTimeout(status,1000)}function preview(){v.src='/preview?t='+Date.now()}v.onload=()=>{loaded++;setTimeout(preview,150)};v.onerror=()=>setTimeout(preview,1000);if(browserPreview)preview();status()</script>'''
html=html.replace(b'PREVIEW_ENABLED',b'true' if a.browser_preview else b'false')
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  try:
   if self.path.startswith('/preview'):
    if not a.browser_preview:self.send_error(404);return
    with urllib.request.urlopen('http://127.0.0.1:%d/api/preview.jpg'%a.renderer_port,timeout=2) as q:data=q.read()
    typ='image/jpeg'
   elif self.path=='/status':
    snapshot=dict(state)
    try:
     with urllib.request.urlopen('http://127.0.0.1:%d/api/status'%a.renderer_port,timeout=1) as q:snapshot['renderer']=json.load(q)
    except Exception:pass
    data=json.dumps(snapshot).encode();typ='application/json'
   else:data=html;typ='text/html'
   self.send_response(200);self.send_header('Content-Type',typ);self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
  except Exception:self.send_error(503)
 def do_POST(self):
  if self.path!='/stop':self.send_error(404);return
  stop.set();self.send_response(200);self.end_headers();self.wfile.write(b'Stopping only this demo')
server=ThreadingHTTPServer(('127.0.0.1',a.port),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
webbrowser.open('http://127.0.0.1:%d'%a.port)
media=r/'demo-10s.avi'
try:
 if not media.exists():
  subprocess.run(['gst-launch-1.0.exe','-q','videotestsrc','num-buffers=500','pattern=ball','!','video/x-raw,format=I420,width=160,height=90,framerate=50/1','!','jpegenc','!','queue','!','mux.','audiotestsrc','num-buffers=500','samplesperbuffer=960','wave=sine','freq=440','volume=0.01','!','audio/x-raw,format=S16LE,rate=48000,channels=2','!','queue','!','mux.','avimux','name=mux','!','filesink','location='+media.as_posix()],check=True,timeout=90,creationflags=child_flags)
 for n in range(a.loops):
  if stop.is_set() or (r/'STOP').exists():break
  d=r/('run-%d-%d'%(int(time.time()),n));d.mkdir();pipeline=d/'source.pipeline';pipeline.write_text('filesrc location="'+media.as_posix()+'" ! avidemux name=d d.video_0 ! queue ! jpegdec ! videoconvert ! @VIDEO_SINK@ d.audio_0 ! queue ! audioconvert ! @AUDIO_SINK@\n')
  state.update(phase='playing',iteration=n+1,loops=a.loops,session=str(d/'session'))
  with (d/'producer.log').open('w') as pl,(d/'renderer.log').open('w') as rl:
   producer=subprocess.Popen([str(b/'framelink_av_producer.exe'),str(b/'gstframelink.dll'),'--session',str(d/'session'),'--format','BGRA','--frames','500','--input-pipeline',str(pipeline),'--timeout-ms','60000'],stdout=pl,stderr=subprocess.STDOUT,creationflags=child_flags)
   render=subprocess.Popen([a.renderer,'--serve','m5-visible']+(['--headless','-1'] if a.browser_preview else [])+['--av-exit-on-end','--control-port',str(a.renderer_port),'--av-session',str(d/'session'),'--av-plugin',str(b/'gstframelink.dll'),'--av-format','BGRA','--av-audio-sink','autoaudiosink','--av-clock','producer'],cwd=d,stdout=rl,stderr=subprocess.STDOUT,creationflags=child_flags)
   children=[producer,render];deadline=time.monotonic()+90
   while any(x.poll() is None for x in children) and time.monotonic()<deadline and not stop.wait(.2) and not (r/'STOP').exists():pass
   for x in children:
    if x.poll() is None:x.terminate()
   for x in children:
    try:x.wait(timeout=5)
    except subprocess.TimeoutExpired:x.kill();x.wait()
   state['exit_codes']=[x.returncode for x in children]
   (d/'result.json').write_text(json.dumps({'exit_codes':state['exit_codes']},indent=2))
   if any(state['exit_codes']) and not stop.is_set():state['phase']='failed';break
   state['phase']='restarting finite clip';stop.wait(1)
finally:
 for x in children:
  if x.poll() is None:x.kill();x.wait()
 state['phase']='stopped';(r/'final-status.json').write_text(json.dumps(state,indent=2));server.shutdown()
