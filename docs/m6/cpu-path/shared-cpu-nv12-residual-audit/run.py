from pathlib import Path
import subprocess,json,time,sys,hashlib,shlex
root=Path(__file__).parent;seconds=int(sys.argv[1]);tag='nv12sample'+str(seconds);adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];base='/data/local/tmp/framelink-residual-audit';remote=base+'/'+tag
s={'state':'preparing','started':time.time(),'seconds':seconds,'scope':'diagnostic; not qualification','build':json.loads((root/'build.json').read_text())}
def save():(root/(tag+'-status.json')).write_text(json.dumps(s,indent=2))
def run(name,args,timeout=60):
 with (root/(tag+'-'+name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
save()
try:
 assert subprocess.check_output(adb+['shell','sha256sum '+base+'/fl_gst_av_bench'],text=True).split()[0]==s['build']['binary_sha256']
 assert run('mkdir',adb+['shell','mkdir '+remote])==0
 args=['./fl_gst_av_bench','static','--shared-cpu','--seconds',str(seconds),'--size','1920x1080','--fps','60','--format','NV12','--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms','17','--transport-only','--no-output-preroll','--output-queue-buffers','1','--raw','--out',remote,'--label','audit']
 s.update(state='running',command='cd '+base+' && '+shlex.join(args));save()
 s['exit']=run('run',adb+['shell',s['command']],seconds+90)
 s['pull_exit']=run('pull',adb+['pull',remote,str(root/tag)],90)
 d=root/tag;f=dict(l.split('=',1) for l in (d/'audit.summary.txt').read_text().splitlines() if '=' in l)
 s['counts']={k:f.get(k) for k in ['consumer.frames','consumer.samples','producer.pushed.frames','producer.pushed.samples']}
 assert s['exit']==s['pull_exit']==0 and f['complete']=='yes'
 assert all(s['counts'][k]==str(seconds*(60 if k.endswith('frames') else 48000)) for k in s['counts'])
 log=(root/(tag+'-run.log')).read_text();s['consumer_stage_rows']=sum(l.startswith('AUDIT consumer ') for l in log.splitlines());s['sink_stage_rows']=sum(l.startswith('AUDIT sink ') for l in log.splitlines());s['fallbacks']=sum(l.startswith('AUDIT output_fallback ') for l in log.splitlines())
 assert s['consumer_stage_rows']==s['sink_stage_rows']==seconds*60
 a=['python3','<linux-home>/dev/framelink-m6/tools/m6_raw_timing.py',str(d/'audit-producer.txt.push.csv'),str(d/'audit-consumer.txt.video.csv'),'--expected',str(seconds*60),'--budget-ms','17','--audio',str(d/'audit-consumer.txt.audio.csv'),'--producer-audio',str(d/'audit-producer.txt.audio-push.csv'),'--expected-audio-blocks',str(seconds*100)]
 with (root/(tag+'-correlation.json')).open('w') as f:s['analysis_exit']=subprocess.run(a,stdout=f,stderr=subprocess.STDOUT,timeout=60).returncode
 assert s['analysis_exit'] in [0,1]
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
