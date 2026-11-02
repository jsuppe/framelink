import argparse,os,pathlib,subprocess,json
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--output',required=True);a=p.parse_args()
b=pathlib.Path(a.build).resolve();root=pathlib.Path(a.output).resolve();root.mkdir(exist_ok=True,parents=True)
ext='.exe' if os.name=='nt' else '';plugin=b/('gstframelink.dll' if ext else 'libgstframelink.so')
for n,stall in enumerate([0,55,55,55]):
 out=root/str(n);out.mkdir(exist_ok=True)
 common=[str(plugin),'--session',str(out/'session'),'--frames','40','--timeout-ms','4000']
 env=dict(os.environ,FRAMELINK_TRACE=str(out/'trace'),FRAMELINK_TRACE_EVENTS='1048576')
 children=[]
 with (out/'producer.log').open('w') as plog,(out/'consumer.log').open('w') as clog:
  try:
   producer=subprocess.Popen([str(b/('framelink_av_producer'+ext)),*common],stdout=plog,stderr=subprocess.STDOUT,env=dict(env,FRAMELINK_TEST_PRODUCER_STALL_MS=str(stall)));children.append(producer)
   consumer=subprocess.Popen([str(b/('framelink_av_consumer'+ext)),*common,'--stream','--clock-policy','producer','--audio-sink','fakesink'],stdout=clog,stderr=subprocess.STDOUT,env=dict(env,FRAMELINK_TEST_PREROLL_DELAY_MS='1500'));children.append(consumer)
   codes=[producer.wait(timeout=12),consumer.wait(timeout=12)]
  finally:
   for c in children:
    if c.poll() is None:c.kill();c.wait()
 result={'case':n,'stall_ms':stall,'codes':codes}
 for role in ['producer','consumer']:
  rows=[json.loads(l) for l in (out/(role+'.log')).read_text().splitlines() if l.startswith('{')]
  result[role]=[r for r in rows if r.get('event') in ['producer-pacing','video-transport-summary','video-source-sequence-gap','playback-complete']]
 print(json.dumps(result),flush=True)
 assert codes==[0,0],result
