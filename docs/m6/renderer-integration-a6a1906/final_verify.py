from pathlib import Path
import subprocess,json,hashlib,os,datetime
r=Path(__file__).parent;out=r/'results';cfg=json.loads((r/'config.json').read_text());overlay=json.loads((r/'zero-source-pins.json').read_text());checks={}
for tree,c in cfg['archives'].items():
 for p,h in c['files'].items():
  expected=overlay['files'].get(p,h) if tree=='renderer' else h
  assert hashlib.sha256((r/tree/p).read_bytes()).hexdigest()==expected,(tree,p)
 checks[tree+'_source_files']=len(c['files'])
old=json.loads((out/'binary-sha256.json').read_text());after={p:hashlib.sha256((r/'build'/p).read_bytes()).hexdigest() for p in old}
changed=[p for p in old if old[p]!=after[p]];assert changed==['Release\\vkrenderer.exe'],changed
(out/'final-binary-sha256.json').write_text(json.dumps(after,indent=2));checks['only_changed_binary']=changed;checks['renderer_commit']=overlay['renderer_commit']
env=dict(os.environ);env['PATH']=str(r/'build/Release')+';'+str(r/'build/framelink/Release')+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH'];exe=str(r/'build/Release/vkrenderer.exe');rows=[]
for args,want in [(['--av-latency-ms',v,'--validation-sentinel'],'usage:') for v in ['0','1','100','2000']]+[(['--av-latency-ms',''],'invalid A/V numeric option'),(['--av-latency-ms','2001'],'--av-latency-ms must be 0..2000')]+[([opt,'0'],'invalid A/V numeric option') for opt in ['--av-width','--av-fps','--av-timeout-ms']]:
 p=subprocess.run([exe]+args,env=env,capture_output=True,text=True,timeout=5);assert p.returncode==1 and want in p.stderr,(args,p.returncode,p.stderr);rows.append({'args':args,'expected_marker':want,'exit':p.returncode,'stderr':p.stderr})
checks['cli_checks']=len(rows);checks['utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'cli-controls.json').write_text(json.dumps(rows,indent=2));(out/'final-verification.json').write_text(json.dumps(checks,indent=2));print(json.dumps(checks))
