"""Exclusive Windows final build/full CTest/API and unchanged-client compatibility."""
from pathlib import Path
import datetime,glob,hashlib,json,os,shutil,subprocess,sys
import verify
ROOT=Path(__file__).resolve().parent;OUT=ROOT/'results';SRC=ROOT/'source';BUILD=ROOT/'build';OUT.mkdir(exist_ok=False)
SPEC=json.loads((ROOT/'config.json').read_text());REF=json.loads((ROOT/'references.json').read_text())
state={'revision':SPEC['revision'],'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'failures':[]}
def save():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def record(name,obj):(OUT/name).write_text(json.dumps(obj,indent=2)+'\n')
def run(cmd,name,timeout=120,cwd=ROOT,required=True):
 state['step']=name;state['command']=[str(x) for x in cmd];save()
 with (OUT/(name+'.log')).open('w') as f:
  p=subprocess.Popen(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT);state['owned_pid']=p.pid;save()
  try:rc=p.wait(timeout)
  except subprocess.TimeoutExpired:
   subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=f,stderr=subprocess.STDOUT,timeout=30);p.wait(timeout=30);raise RuntimeError(name+' timed out; owned tree cleanup attempted')
 state.pop('owned_pid',None);state[name+'_exit']=rc;save()
 if rc:
  state['failures'].append(name)
  if required:raise RuntimeError(name+' failed')
 return rc
save()
try:
 for key in list(os.environ):
  if key.upper().startswith(('FRAMELINK_','FL_BENCH_','FL_FAULT','GST_DEBUG','GST_TRACER')):os.environ.pop(key,None)
 os.environ.update(MSBUILDDISABLENODEREUSE='1',CTEST_PARALLEL_LEVEL='1',GST_REGISTRY=str(ROOT/'registry.bin'))
 os.environ['PATH']='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+os.environ['PATH']
 gst_inspect=Path('C:/Program Files/gstreamer/1.0/msvc_x86_64/bin/gst-inspect-1.0.exe')
 assert gst_inspect.is_file(),'required GStreamer inspection executable missing'
 run([str(gst_inspect),'--version'],'gst-tools-preflight',30)
 baseline=Path(REF['baseline_client_directory']);clients={name:sha(baseline/name) for name in REF['baseline_clients']};assert clients==REF['baseline_clients'],'existing old-client hash mismatch; never rebuild automatically';record('baseline-client-hashes.json',clients)
 assert sha(ROOT/'source.tar')==SPEC['archive_sha256'],'source archive mismatch'
 SRC.mkdir();run(['tar','-xf',str(ROOT/'source.tar'),'-C',str(SRC)],'extract')
 for name,digest in SPEC['source_pins'].items():assert sha(SRC/name)==digest,'source pin mismatch '+name
 query=BUILD/'.cmake/api/v1/query';query.mkdir(parents=True);(query/'codemodel-v2').touch()
 run(['cmake','-S',str(SRC),'-B',str(BUILD),'-G','Visual Studio 17 2022','-DFRAMELINK_BUILD_TESTS=ON','-DFRAMELINK_BUILD_GST=ON','-DFRAMELINK_BUILD_SAMPLES=ON'],'configure',240)
 run(['cmake','--build',str(BUILD),'--config','Release','--target','ALL_BUILD','-j','4','--','/nr:false'],'build-all',1800)
 # File API can advertise optional Release PDBs even when this build emits none.
 # Require each runtime binary and every declared non-PDB artifact (including .lib).
 reply=BUILD/'.cmake/api/v1/reply';index=json.loads(sorted(reply.glob('index-*.json'))[-1].read_text());cm=json.loads((reply/index['reply']['codemodel-v2']['jsonFile']).read_text());release=next(c for c in cm['configurations'] if c['name']=='Release');models=[]
 for target in release['targets']:
  t=json.loads((reply/target['jsonFile']).read_text())
  if t['type'] in ('EXECUTABLE','SHARED_LIBRARY','MODULE_LIBRARY'):models.append(t)
 assert models,'no executable/library targets configured'
 targets=[verify.target_artifacts(BUILD,t) for t in models];record('configured-targets-before-repair.json',targets)
 missing=[t['name'] for t in targets if not t['pass']]
 for offset in range(0,len(missing),15):run(['cmake','--build',str(BUILD),'--config','Release','--target',*missing[offset:offset+15],'-j','4','--','/nr:false'],'build-missing-'+str(offset),900)
 targets=[verify.target_artifacts(BUILD,t) for t in models];record('configured-targets.json',targets)
 assert all(t['pass'] for t in targets),'configured target runtime/link artifacts still missing'
 run(['ctest','--test-dir',str(BUILD),'-C','Release','--show-only=json-v1'],'inventory')
 inv=verify.inventory(OUT/'inventory.log');assert set(REF['prior_windows_test_names'])<={t['name'] for t in inv['tests']},'previous Windows test registrations missing';state['expected_tests']=len(inv['tests']);save()
 # Verify resolved executable/plugin paths passed to Python wrapper tests as well.
 for t in inv['tests']:
  for arg in t['command']:
   p=Path(arg)
   if p.is_absolute() and p.suffix.lower() in ('.exe','.dll','.py'):assert p.is_file(),'registered command artifact missing: '+arg
 bins=BUILD/'Release';os.environ['PATH']=str(bins)+';'+os.environ['PATH'];os.environ['GST_PLUGIN_PATH']=str(bins)
 binary_names=['framelink.dll','framelink_gst_session.dll','gstframelink.dll'];before={n:sha(bins/n) for n in binary_names};record('candidate-binaries-before.json',before)
 dump=sorted(glob.glob('C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe'))[-1]
 for lib in ['framelink','framelink_gst_session']:run([dump,'/exports',str(bins/(lib+'.dll'))],'exports-'+lib)
 record('candidate-plugin-path.json',{'path':str(bins/'gstframelink.dll')})
 for name in ['framelinksink','framelinksrc','framelinkavsink','framelinkavsrc']:run([str(gst_inspect),name],'inspect-'+name)
 ap=verify.api(OUT,REF);record('api-comparison.json',ap)
 # Plain C11 translation unit including both owner typedef sites (duplicate typedef guard).
 c11=ROOT/'c11_owner_headers.c';c11.write_text('#include "framelink_d3d11.h"\n#include "framelink_session_transport.h"\n#include "framelink_d3d11.h"\nint fl_c11_probe(flD3D11DeviceOwner* o){return o!=0;}\n')
 gst=Path('C:/Program Files/gstreamer/1.0/msvc_x86_64')
 vc=sorted(glob.glob('C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Auxiliary/Build/vcvars64.bat'))[-1]
 inc=' '.join('/I"'+str(p)+'"' for p in (SRC/'include',SRC/'gst',BUILD,gst/'include/glib-2.0',gst/'lib/glib-2.0/include',gst/'include/gstreamer-1.0'))
 bat=ROOT/'c11_owner_headers.bat';bat.write_text('@call "'+vc+'" >nul\ncl /nologo /c /TC /std:c11 /W3 '+inc+' "'+str(c11)+'" /Fo"'+str(ROOT/'c11_owner_headers.obj')+'"\n@exit /b %ERRORLEVEL%\n')
 run(['cmd','/c',str(bat)],'c11-owner-headers',120)
 if not ap['pass']:state['failures'].append('api-contract')
 # Exactly the entire generated inventory, serially. No filtering or retries.
 run(['ctest','--test-dir',str(BUILD),'-C','Release','--output-on-failure','--output-junit',str(OUT/'full.junit.xml'),'--timeout','420','--no-tests=error','-j','1'],'full-ctest',7200,required=False)
 report=verify.ctest(OUT/'inventory.log',OUT/'full.junit.xml');record('ctest-coverage.json',report)
 if not report['pass']:state['failures'].append('ctest-coverage')
 # Reuse the exact previously built baseline clients; never configure old source.
 runtime=ROOT/'compat-runtime';runtime.mkdir()
 for name in REF['baseline_clients']:shutil.copyfile(baseline/name,runtime/name)
 for name in ['framelink.dll','framelink_gst_session.dll']:shutil.copyfile(bins/name,runtime/name)
 comp={'client_sha256':{n:sha(runtime/n) for n in REF['baseline_clients']},'candidate_dll_sha256':{n:sha(runtime/n) for n in ['framelink.dll','framelink_gst_session.dll']},'exits':{}}
 assert comp['client_sha256']==REF['baseline_clients'];assert all(comp['candidate_dll_sha256'][n]==before[n] for n in comp['candidate_dll_sha256'])
 for name in REF['baseline_clients']:
  run([dump,'/imports',str(runtime/name)],'compat-imports-'+name)
  comp['exits'][name]=run([str(runtime/name)],'compat-run-'+name,120,cwd=runtime,required=False)
 comp['pass']=all(rc==0 for rc in comp['exits'].values());record('compatibility.json',comp)
 after={n:sha(bins/n) for n in binary_names};record('candidate-binaries-after.json',after);assert after==before,'candidate DLL changed during validation'
 state['state']='finished' if not state['failures'] else 'failed'
except Exception as error:state.update(state='failed',error=str(error))
finally:
 for name in ['LastTest.log','LastTestsFailed.log']:
  path=BUILD/'Testing/Temporary'/name
  if path.exists():shutil.copyfile(path,OUT/name)
 for path in [BUILD/'CMakeCache.txt']:
  if path.exists():shutil.copyfile(path,OUT/path.name)
 state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2),flush=True);sys.exit(0 if state['state']=='finished' else 1)
