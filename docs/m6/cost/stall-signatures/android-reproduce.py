from pathlib import Path
import subprocess,json,time,shlex,csv
root=Path('<linux-home>/dev/evidence/m6-cost/android-stall-rgba-controls');root.mkdir(exist_ok=True)
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];remote='/data/local/tmp/framelink-m6-20261106'
s={'state':'running','started':time.time(),'revision':'benchmark controlled source/post-push stall validation','scope':'bounded 65536-event per-recorder diagnostic stage timing, not throughput qualification','cases':[]}
def save():(root/'status.json').write_text(json.dumps(s,indent=2))
save()
try:
 subprocess.run(adb+['push','<linux-home>/dev/build/framelink-m6-arm64/fl_gst_av_bench',remote+'/fl_gst_av_bench'],check=True)
 for stage in ['none','source','post-push']:
  dims,fps,budget='3840x2160',30,34
  label=f'stall-{stage}-{int(time.time())}';directory=remote+'/'+label
  subprocess.run(adb+['shell','mkdir '+directory],check=True)
  opts=f'--seconds 5 --size {dims} --fps {fps} --format RGBA --pool 2 --video-queue 1 --latency-ms 0 --arrival-budget-ms {budget} --transport-only --no-output-preroll --output-queue-buffers 1 --raw --native'
  if stage!='none':opts+=f' --video-stall-stage {stage} --video-stall-frame 90 --video-stall-ms 150'
  procs=[];logs=[]
  for role in ['consumer','producer']:
   cmd=f'cd {directory} && export TMPDIR={remote} LD_LIBRARY_PATH={remote} FRAMELINK_TRACE={directory}/{role}-core FRAMELINK_GST_TRACE={directory}/{role}-gst FRAMELINK_TRACE_EVENTS=65536 FRAMELINK_GST_TRACE_EVENTS=65536 && {remote}/fl_gst_av_bench static {opts} --role {role} --session {directory}/session --result {directory}/{role}.txt'
   f=(root/(label+'-'+role+'.log')).open('w');logs.append(f);procs.append(subprocess.Popen(adb+['shell',cmd],stdout=f,stderr=subprocess.STDOUT));time.sleep(.2)
  exits=[p.wait(timeout=120) for p in reversed(procs)]
  for f in logs:f.close()
  subprocess.run(adb+['pull',directory,str(root)],check=True,stdout=subprocess.DEVNULL,timeout=60)
  assert exits==[0,0],str(exits)
  fields=dict(l.split('=',1) for l in (root/label/'consumer.txt').read_text().splitlines() if '=' in l)
  assert fields.get('frames')=='150' and fields.get('samples')=='240000',str(fields)
  producer=dict(l.split('=',1) for l in (root/label/'producer.txt').read_text().splitlines() if '=' in l)
  assert fields.get('result')=='EOS' and fields.get('audio.contiguous')=='yes' and fields.get('queue.accounting')=='complete',str(fields)
  assert producer.get('injection.executed')==('0' if stage=='none' else '1'),str(producer)
  if stage!='none':assert int(producer['injection.elapsed_us'])>=145000
  push=list(csv.DictReader((root/label/'producer.txt.push.csv').open()));arrival=list(csv.DictReader((root/label/'consumer.txt.video.csv').open()))
  assert len(push)==len(arrival)==150
  assert len({int(y['pts_ns'])-int(x['pts_ns']) for x,y in zip(push,arrival)})==1
  a=int(push[89]['push_lateness_ns'])/1e6;b=int(arrival[89]['arrival_lateness_ns'])/1e6
  if stage=='source':assert a>=145 and b-a<34,(a,b)
  if stage=='post-push':assert a<34 and b-a>=145,(a,b)
  s['cases'].append({'label':label,'producer_consumer_exit':exits,'push_ms':a,'arrival_ms':b,'residual_ms':b-a,'frames':150,'samples':240000});save()
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='finished' else 1)
