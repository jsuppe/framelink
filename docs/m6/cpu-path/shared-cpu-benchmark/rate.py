import pathlib,subprocess,json,time,sys,hashlib
fmt,size,fps,seconds=sys.argv[1:];fps=int(fps);seconds=int(seconds)
assert fmt in ['BGRA','NV12'] and (size,fps) in [('1920x1080',60),('3840x2160',30)]
repo=pathlib.Path('<linux-home>/dev/framelink-m6');build=pathlib.Path('<linux-home>/dev/build/framelink-m6-linux')
label=f'linux-shared-{fmt.lower()}-{size}-{fps}-{seconds}'
root=pathlib.Path(__file__).parent/label;root.mkdir(exist_ok=False)
budget=17 if fps==60 else 34
cmd=[str(build/'fl_gst_av_bench'),str(build/'libgstframelink.so'),'--shared-cpu','--seconds',str(seconds),'--size',size,'--fps',str(fps),'--format',fmt,'--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms',str(budget),'--transport-only','--no-output-preroll','--output-queue-buffers','1','--raw','--out',str(root),'--label',label]
s={'state':'running','started':time.time(),'revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'command':cmd,'binary_sha256':{p:hashlib.sha256((build/p).read_bytes()).hexdigest() for p in ['fl_gst_av_bench','libgstframelink.so','libframelink_gst_session.so','libframelink.so']}}
def save():(root/'status.json').write_text(json.dumps(s,indent=2))
save()
with (root/'host-load.txt').open('w') as f:subprocess.run(['ps','-eo','pid,pcpu,pmem,comm'],stdout=f)
try:
 with (root/'run.log').open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=seconds+120)
 s['exit']=r.returncode
 fields=dict(l.split('=',1) for l in (root/(label+'.summary.txt')).read_text().splitlines() if '=' in l)
 s['complete']=fields.get('complete')
 analysis=['python3',str(repo/'tools/m6_raw_timing.py'),str(root/(label+'-producer.txt.push.csv')),str(root/(label+'-consumer.txt.video.csv')),'--expected',str(seconds*fps),'--budget-ms',str(budget),'--audio',str(root/(label+'-consumer.txt.audio.csv')),'--producer-audio',str(root/(label+'-producer.txt.audio-push.csv')),'--expected-audio-blocks',str(seconds*100)]
 with (root/'correlation.json').open('w') as f:a=subprocess.run(analysis,stdout=f,stderr=subprocess.STDOUT)
 s['analysis_exit']=a.returncode;s['state']='finished'
 s['resource_slopes']={k:v for k,v in fields.items() if 'mem' in k and 'slope' in k}
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s,indent=2),flush=True)
raise SystemExit(0 if s.get('exit')==0 and s.get('complete')=='yes' and s.get('analysis_exit')==0 else 1)
