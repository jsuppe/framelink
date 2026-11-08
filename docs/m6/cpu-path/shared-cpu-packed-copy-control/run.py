from pathlib import Path
import subprocess,json,time,sys,hashlib,shlex
root=Path(__file__).parent;seconds=int(sys.argv[1]);tag=sys.argv[2];mode=sys.argv[3];mask='ff';adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];base='/data/local/tmp/framelink-packed-copy';remote=base+'/'+tag
s={'state':'preparing','started':time.time(),'seconds':seconds,'scope':'consumer row versus packed-plane copy diagnostic; not qualification','copy_mode':mode,'build':json.loads((root.parent/'build.json').read_text())}
def save():(root/(tag+'-status.json')).write_text(json.dumps(s,indent=2))
def run(name,args,timeout=60):
 with (root/(tag+'-'+name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
save()
try:
 assert subprocess.check_output(adb+['shell','sha256sum '+base+'/fl_gst_av_bench'],text=True).split()[0]==s['build']['binary_sha256']
 assert run('mkdir',adb+['shell','mkdir '+remote])==0
 args=(['env','FL_AUDIT_PLANE_COPY=1'] if mode=='plane' else ['env','-u','FL_AUDIT_PLANE_COPY'])+['taskset',mask,'./fl_gst_av_bench','static','--shared-cpu','--seconds',str(seconds),'--size','3840x2160','--fps','30','--format','BGRA','--pool','2','--video-queue','1','--latency-ms','0','--arrival-budget-ms','34','--transport-only','--no-output-preroll','--output-queue-buffers','1','--raw','--out',remote,'--label','audit']
 s.update(state='running',command='cd '+base+' && '+shlex.join(args));save()
 with (root/(tag+'-run.log')).open('w') as log:
  proc=subprocess.Popen(adb+['shell',s['command']],stdout=log,stderr=subprocess.STDOUT)
  time.sleep(2)
  inspect='for p in $(pidof fl_gst_av_bench); do for t in /proc/$p/task/*; do echo TASK:$t; cat $t/status | grep -E "^(Name|Pid|Cpus_allowed_list):"; done; done'
  sample=subprocess.check_output(adb+['shell',inspect],text=True,timeout=30)
  (root/(tag+'-affinity.txt')).write_text(sample)
  allowed=[l.split(':',1)[1].strip() for l in sample.splitlines() if l.startswith('Cpus_allowed_list:')]
  s['observed_thread_affinities']=allowed
  s['affinity_verified']=bool(allowed) and all(v==('6-7' if mask=='c0' else '0-7') for v in allowed)
  s['exit']=proc.wait(timeout=seconds+90)
 assert s['affinity_verified'], 'effective affinity not as requested'
 s['pull_exit']=run('pull',adb+['pull',remote,str(root/tag)],90)
 d=root/tag;f=dict(l.split('=',1) for l in (d/'audit.summary.txt').read_text().splitlines() if '=' in l)
 s['counts']={k:f.get(k) for k in ['consumer.frames','consumer.samples','producer.pushed.frames','producer.pushed.samples']}
 assert s['exit']==s['pull_exit']==0 and f['complete']=='yes'
 assert all(s['counts'][k]==str(seconds*(30 if k.endswith('frames') else 48000)) for k in s['counts'])
 log=(root/(tag+'-run.log')).read_text();s['consumer_stage_rows']=sum(l.startswith('AUDIT consumer ') for l in log.splitlines());s['sink_stage_rows']=sum(l.startswith('AUDIT sink ') for l in log.splitlines());s['fallbacks']=sum(l.startswith('AUDIT output_fallback ') for l in log.splitlines())
 assert s['consumer_stage_rows']==s['sink_stage_rows']==seconds*30
 assert log.count('AUDIT plane_mode enabled='+str(int(mode=='plane')))==1
 a=['python3','<linux-home>/dev/framelink-m6/tools/m6_raw_timing.py',str(d/'audit-producer.txt.push.csv'),str(d/'audit-consumer.txt.video.csv'),'--expected',str(seconds*30),'--budget-ms','34','--audio',str(d/'audit-consumer.txt.audio.csv'),'--producer-audio',str(d/'audit-producer.txt.audio-push.csv'),'--expected-audio-blocks',str(seconds*100)]
 with (root/(tag+'-correlation.json')).open('w') as f:s['analysis_exit']=subprocess.run(a,stdout=f,stderr=subprocess.STDOUT,timeout=60).returncode
 assert s['analysis_exit'] in [0,1]
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
