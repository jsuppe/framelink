import os,pathlib,subprocess,tempfile,json
out=pathlib.Path('/tmp/fl-m3-playback-evidence');out.mkdir(exist_ok=True)
sink='framelink_m3_smoke'
module=subprocess.check_output(['pactl','load-module','module-null-sink','sink_name='+sink],text=True).strip()
try:
 for fmt in ['BGRA','NV12']:
  root=tempfile.mkdtemp(prefix='fl-m3-play-');session=str(pathlib.Path(root)/'session')
  env=os.environ.copy();env['PULSE_SINK']=sink
  common=['/tmp/framelink-audio-build/libgstframelink.so','--session',session,'--frames','30','--format',fmt,'--timeout-ms','15000']
  with (out/(fmt+'-consumer.log')).open('w') as c,(out/(fmt+'-producer.log')).open('w') as p:
   consumer=subprocess.Popen(['xvfb-run','-a','/tmp/framelink-audio-build/framelink_av_consumer',*common,'--play'],env=env,stdout=c,stderr=subprocess.STDOUT)
   producer=subprocess.Popen(['/tmp/framelink-audio-build/framelink_av_producer',*common],env=env,stdout=p,stderr=subprocess.STDOUT)
   try:
    pc=producer.wait(timeout=20);cc=consumer.wait(timeout=20)
    assert pc==0 and cc==0,(fmt,pc,cc)
   finally:
    for child in [producer,consumer]:
     if child.poll() is None:child.kill();child.wait()
  lines=(out/(fmt+'-consumer.log')).read_text().splitlines();events=[json.loads(x) for x in lines if x.startswith('{')];assert any(e.get('ok') is True for e in events),lines
  print(fmt,'--play virtual X display + dedicated null audio sink passed',flush=True)
finally:
 subprocess.run(['pactl','unload-module',module],check=True)
