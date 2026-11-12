from pathlib import Path
import subprocess,os,json,hashlib,tarfile,datetime
root=Path(__file__).parent;out=root/'results';out.mkdir(exist_ok=False);cfg=json.loads((root/'config.json').read_text());s={'state':'preparing','renderer':cfg['renderer'],'framelink':cfg['framelink'],'commands':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def run(args,name,timeout):
 s['state']=name;s['commands'].append(args);save()
 with (out/(name+'.log')).open('w') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=timeout)
 s[name+'_exit']=r.returncode;save();assert r.returncode==0,name
save()
try:
 for n,c in cfg['archives'].items():
  archive=root/(n+'.tar');assert hashlib.sha256(archive.read_bytes()).hexdigest()==c['sha256'];dest=root/n;dest.mkdir(exist_ok=False)
  with tarfile.open(archive) as t:t.extractall(dest,filter='data')
  for p,h in c['files'].items():assert hashlib.sha256((dest/p).read_bytes()).hexdigest()==h,p
  s[n+'_verified_files']=len(c['files']);save()
 env=dict(os.environ);env['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH'];env['GSTREAMER_1_0_ROOT_MSVC_X86_64']='C:/Program Files/gstreamer/1.0/msvc_x86_64';env['MSBUILDDISABLENODEREUSE']='1'
 build=root/'build'
 run(['cmake','-S',str(root/'renderer'),'-B',str(build),'-A','x64','-DFRAMELINK_DIR='+str(root/'framelink'),'-DFRAMELINK_BUILD_GST=ON','-DFRAMELINK_BUILD_SAMPLES=ON','-DFRAMELINK_BUILD_TESTS=OFF'],'configure',1200)
 run(['cmake','--build',str(build),'--config','Release','--target','vkrenderer','gstframelink','framelink_av_producer','vkr_av_schedule_test','vkr_av_clock_contract_test','vkr_headless_pacer_test','vkr_nv12_cpu_test','--','/nr:false','/m:2'],'build',1800)
 pins={str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest() for p in build.rglob('*') if p.is_file() and p.suffix.lower() in ['.exe','.dll'] and 'Release' in p.parts};(out/'binary-sha256.json').write_text(json.dumps(pins,indent=2));s['state']='built'
except Exception as e:s.update(state='failed',error=str(e))
s['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save();print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='built' else 1)
