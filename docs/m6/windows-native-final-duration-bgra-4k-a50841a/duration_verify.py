"""Local-only strict receipt joins for a single native duration qualification."""
from pathlib import Path
import hashlib,json,re
import integrated_verify as integrated
import native_verify as native
PAIR_UPLOADS=('remote.py','execute.ps1','collect.py','native_verify.py','integrated_verify.py','prior_checks.py','expected-integrated-references.json','config.json','check_local_native_bench.py','controller.py','prior-validation.json','invocation.json')

def controller(c,revision):
 if c.get('state')!='finished' or c.get('revision')!=revision or c.get('local_checks_pass') is not True or c.get('error') or c.get('collection_error') or any(type(c.get(k)) is not int or c[k]!=0 for k in ('remote_exit','package_exit','pull_exit')):
  raise ValueError('successful exact collected controller required')

def profile(s,seconds):
 if (s.get('width'),s.get('height'),s.get('fps')) not in ((1920,1080,60),(3840,2160,30)) or s.get('format') not in ('BGRA','NV12') or type(s.get('seconds')) is not int or s['seconds']!=seconds:
  raise ValueError('unsupported target profile or duration')
 if not isinstance(s.get('revision'),str) or not re.fullmatch('[a-f0-9]{40}',s['revision']):raise ValueError('invalid exact revision')

def pair_receipts(c,s,status,pin_receipt,paths,dlls,archive_sha,analyzer_sha):
 profile(s,5);controller(c,s['revision'])
 if s.get('artifact_paths')!=paths or s.get('integrated_dll_sha256')!=dlls or s.get('source_archive_sha256')!=archive_sha or s.get('source_analyzer_sha256')!=analyzer_sha:
  raise ValueError('pair provenance differs from integrated source/artifacts')
 if any(s.get('binary_directory')+'/'+n!=p for n,p in paths.items()):raise ValueError('pair binary directory differs')
 pins=native.receipt(status,s)
 if c.get('binary_sha256')!=pins:raise ValueError('pair controller binary pins differ')
 if pin_receipt.get('binary_sha256')!=pins or pin_receipt.get('revision')!=s['revision'] or pin_receipt.get('artifact_paths')!=paths or pin_receipt.get('source_archive_sha256')!=archive_sha:
  raise ValueError('pair initial pin provenance differs')
 return pins

def pair_files(pair):
 pair=Path(pair);data=pair/'data';integrated.verify_collection(data)
 uploads=json.loads((data/'artifact-sha256.json').read_text())
 if set(uploads)!=set(PAIR_UPLOADS):raise ValueError('unexpected pair upload inventory')
 for name in (*PAIR_UPLOADS,'artifact-sha256.json'):
  raw=(data/name).read_bytes()
  if raw!=(pair/name).read_bytes():raise ValueError('pair upload identity differs: '+name)
  if name in uploads and hashlib.sha256(raw).hexdigest()!=uploads[name]:raise ValueError('pair upload hash differs: '+name)
 if json.loads((data/'idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:raise ValueError('pair final idle missing/busy')
 return data

def duration_receipt(status,spec):
 profile(spec,1800)
 if status.get('state')!='finished' or status.get('revision')!=spec['revision'] or status.get('error'):raise ValueError('duration status/revision failure')
 native.pinset(spec['binary_sha256'],spec['integrated_dll_sha256'])
 for key in ('binary_before','duration_binary_before','duration_binary_after'):
  if status.get(key)!=spec['binary_sha256']:raise ValueError('duration binary pins differ: '+key)
 for name in ('pin-idle-before','duration-idle-before','duration','duration-idle-after','duration-analysis'):
  if type(status.get(name+'_exit')) is not int or status[name+'_exit']!=0:raise ValueError('duration phase incomplete: '+name)
