#!/usr/bin/env python3
import os,pathlib,subprocess,time,json
root=pathlib.Path('C:/Users/jonsu/dev/m5-renderer-baseline-v2');root.mkdir(exist_ok=True)
summary=[]
for fmt in ['BGRA','NV12']:
 for fps in [30,60]:
  for version in ['m4','m5']:
   d=root/f'{version}-{fmt}-{fps}';d.mkdir(exist_ok=True);base=pathlib.Path('C:/Users/jonsu/dev');b=base/('framelink-m4-20261030/build-final/Release' if version=='m4' else 'framelink-m5-pipeline/build/Release');r=base/('vkrender-m4-20261030/build/Release' if version=='m4' else 'vkrender-m5-pipeline/build/Release');channel=d.name
   env=dict(os.environ,VKR_INPUT_FORMATS=fmt.lower(),GST_PLUGIN_PATH=str(b),VKR_TRACE=str(d/'renderer'),FRAMELINK_TRACE=str(d/'transport'));env['GST_REGISTRY']=str(d/'registry.bin');env['PATH']=str(b)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH']
   rc=[str(r/'vkrenderer.exe'),'--serve',channel,'--headless','120','--control-port','0'];pc=['C:/Program Files/gstreamer/1.0/msvc_x86_64/bin/gst-launch-1.0.exe','-q','videotestsrc','is-live=true','pattern=ball','!','video/x-raw,format='+fmt+f',width=1920,height=1080,framerate={fps}/1','!','framelinksink','channel='+channel+'.in.0','sync=true'];start=time.perf_counter()
   with (d/'renderer.log').open('w') as ro,(d/'producer.log').open('w') as po:
    re=subprocess.Popen(rc,env=env,stdout=ro,stderr=subprocess.STDOUT);time.sleep(.4);pr=subprocess.Popen(pc,env=env,stdout=po,stderr=subprocess.STDOUT)
    try: re.wait(timeout=20)
    except subprocess.TimeoutExpired:re.kill();re.wait()
    finally:
     if pr.poll() is None:pr.terminate();pr.wait()
   row={'case':d.name,'wall_s':time.perf_counter()-start,'renderer_exit':re.returncode,'commands':[pc,rc]};summary.append(row);(root/'summary.json').write_text(json.dumps(summary,indent=2));print(row,flush=True)
