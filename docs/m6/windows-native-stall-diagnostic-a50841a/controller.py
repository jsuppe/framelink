from pathlib import Path
import subprocess,base64,hashlib,json,tarfile,shutil,sys,datetime
base=Path(__file__).resolve().parent;repo=Path('<linux-home>/dev/framelink-m6');rev='a50841aad44fbc9431234a4f326bad036d193bf5';rid=sys.argv[1];assert rid.replace('-','').isalnum()
r=base/'runs'/rid;r.mkdir(parents=True,exist_ok=False);remote='<win-home>/dev/framelink-native-stall-'+rid;host='<win-user>@<win-host>';state=dict(state='preparing',revision=rev,remote=remote,started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
def save():(r/'controller-status.json').write_text(json.dumps(state,indent=2)+'\n')
def run(args,name,timeout=120):
 with (r/(name+'.log')).open('w') as f:return subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
def ps(s):return ['ssh','-o','BatchMode=yes',host,'powershell','-NoProfile','-EncodedCommand',base64.b64encode(s.encode('utf-16le')).decode()]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
save();provisioned=False
try:
 archive=r/'source.tar';subprocess.run(['git','-C',str(repo),'archive','--format=tar','-o',str(archive),rev],check=True)
 with tarfile.open(archive) as t:pins={m.name:hashlib.sha256(t.extractfile(m).read()).hexdigest() for m in t.getmembers() if m.isfile()}
 assert pins['test/fl_gst_local_native_bench_win.cpp']==sha(base/'original.cpp')
 spec=dict(revision=rev,archive_sha256=sha(archive),source_pins=pins,diagnostic_sha256=sha(base/'diagnostic.cpp'));(r/'config.json').write_text(json.dumps(spec,indent=2)+'\n')
 files=['original.cpp','diagnostic.cpp','diagnostic.patch','provenance.json','remote.py','execute.ps1','collect.py','integrated_verify.py','analyze_diagnostic.py','check_local_native_bench.py']
 for n in files:shutil.copy2(base/n,r/n)
 shutil.copy2(__file__,r/'controller.py');files+=['config.json','source.tar','controller.py'];(r/'artifact-sha256.json').write_text(json.dumps({n:sha(r/n) for n in files},indent=2)+'\n')
 assert run(ps("$ErrorActionPreference='Stop';New-Item -ItemType Directory '"+remote+"' -ErrorAction Stop|Out-Null"),'provision')==0;provisioned=True
 for n in files+['artifact-sha256.json']:assert run(['scp','-q',str(r/n),host+':'+remote+'/'+n],'upload-'+n,240)==0
 state['state']='running';save();state['remote_exit']=run(['ssh','-o','BatchMode=yes',host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/execute.ps1','-RuntimeLaneReleased'],'remote',2400)
except Exception as e:state.update(error=str(e),remote_may_still_run=isinstance(e,subprocess.TimeoutExpired))
finally:
 if provisioned and not state.get('remote_may_still_run'):
  try:
   state['package_exit']=run(ps("Set-Location '"+remote+"';python -E collect.py;exit $LASTEXITCODE"),'package',180);assert state['package_exit']==0
   state['pull_exit']=run(['scp','-q',host+':'+remote+'/evidence.tar',str(r/'evidence.tar')],'pull',240);assert state['pull_exit']==0
   d=r/'data';d.mkdir()
   with tarfile.open(r/'evidence.tar') as t:t.extractall(d,filter='data')
   sys.path.insert(0,str(base));import integrated_verify
   integrated_verify.verify_collection(d)
   for n in files:
    if n!='source.tar':assert (d/n).read_bytes()==(r/n).read_bytes(),n
   idle=json.loads((d/'host/idle-final.json').read_text(encoding='utf-8-sig'));assert idle['activeRuntime']==[]
   receipt=json.loads((d/'results/status.json').read_text());state['remote_status']=receipt;state['collected_hashes']=len(json.loads((d/'collected-sha256.json').read_text()));state['collection_verified']=True
  except Exception as e:state['collection_error']=str(e)
 state['state']='collected' if state.get('collection_verified') else 'failed';state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps(state,indent=2));sys.exit(0 if state.get('remote_exit')==0 and state.get('collection_verified') else 1)
