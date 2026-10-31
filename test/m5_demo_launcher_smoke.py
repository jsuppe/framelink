import tempfile,pathlib,subprocess,os,json,socket
# POSIX orchestration smoke with fake children; not a renderer/device test.
script=str(pathlib.Path(__file__).with_name('m5_visible_demo.py'))
for browser in (False,True):
 with tempfile.TemporaryDirectory() as t:
  root=pathlib.Path(t);(root/'demo-10s.avi').write_bytes(b'mock fixture')
  exe=root/'framelink_av_producer.exe';exe.write_text('#!/usr/bin/env python3\n');exe.chmod(0o755)
  renderer=root/'renderer';renderer.write_text('#!/usr/bin/env python3\nimport sys,json,pathlib\npathlib.Path("argv.json").write_text(json.dumps(sys.argv))\n');renderer.chmod(0o755)
  ports=[]
  for _ in range(2):
   with socket.socket() as sock:sock.bind(('127.0.0.1',0));ports.append(sock.getsockname()[1])
  env=dict(os.environ,BROWSER='/bin/true')
  subprocess.run(['python3',script,'--build',t,'--renderer',str(renderer),'--output',t,'--loops','1','--port',str(ports[0]),'--renderer-port',str(ports[1])]+(['--browser-preview'] if browser else []),check=True,env=env,timeout=15)
  args=json.loads(next(root.glob('run-*/argv.json')).read_text())
  assert ('--headless' in args)==browser,args
  assert json.loads((root/'final-status.json').read_text())['exit_codes']==[0,0]
  print('PASS', 'optional-JPEG' if browser else 'native-default',args)
