"""Read-only validators, shared by remote execution and local receipt review."""
from pathlib import Path,PureWindowsPath
import collections,hashlib,json,re,xml.etree.ElementTree as ET
REQUIRED={'local_native_bench_analyzer','win_sha256_rows','gst_local_native_bench_smoke_win','local_decoder_checker','gst_local_decoder_discovery_win','local_decoder_rate_checker','gst_local_decoder_rate_smoke_win','gst_relay_local_bin_pause_win','gst_relay_local_bin_paused_cancel_win'}
def inventory(path):
 data=json.loads(Path(path).read_text());tests=data['tests'];names=[x['name'] for x in tests]
 if not names or len(set(names))!=len(names):raise ValueError('empty or duplicate CTest inventory')
 if REQUIRED-set(names):raise ValueError('required capability registrations absent: '+str(sorted(REQUIRED-set(names))))
 for test in tests:
  props={x['name']:x['value'] for x in test.get('properties',[])}
  if props.get('DISABLED'):raise ValueError('disabled registered test: '+test['name'])
  if not test.get('command'):raise ValueError('unresolved command: '+test['name'])
 return data

def ctest(inventory_path,junit_path):
 data=inventory(inventory_path);expected=[t['name'] for t in data['tests']];doc=ET.parse(junit_path);cases=doc.findall('.//testcase');actual=[c.attrib['name'] for c in cases]
 missing=list((collections.Counter(expected)-collections.Counter(actual)).elements());extra=list((collections.Counter(actual)-collections.Counter(expected)).elements())
 failed=[c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
 skipped=[c.attrib['name'] for c in cases if c.find('skipped') is not None or c.attrib.get('status','').lower() in ('notrun','disabled','skipped')]
 # Some fixtures print SKIP yet exit zero; such a run does not satisfy no-skips.
 printed=[c.attrib['name'] for c in cases if re.search(r'(?im)^\s*(?:\*\*\*\s*)?SKIP(?:PED)?(?:\b|:)',c.findtext('system-out',''))]
 result={'expected_count':len(expected),'reported_count':len(actual),'expected_names':expected,'reported_names':actual,'missing':missing,'unexpected':extra,'failed':failed,'skipped':skipped,'printed_skip':printed}
 result['pass']=not(missing or extra or failed or skipped or printed);return result

def exports(text):return set(re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)',text,re.M))
def api(root,refs):
 root=Path(root);per_dll={}
 for dll in ['framelink.dll','framelink_gst_session.dll']:
  got=exports((root/('exports-'+dll[:-4]+'.log')).read_text());want=set(refs['candidate_exports_by_dll'][dll]);old=set(refs['baseline_exports_by_dll'][dll])
  per_dll[dll]={'removed_candidate':sorted(want-got),'removed_baseline':sorted(old-got),'unexpected':sorted(got-want),'pass':got==want and old<=got}
 actual=exports((root/'exports-framelink.log').read_text())|exports((root/'exports-framelink_gst_session.log').read_text());expected=set(refs['candidate_exports']);baseline=set(refs['baseline_exports'])
 plugin=json.loads((root/'candidate-plugin-path.json').read_text())['path'].replace('\\','/').lower()
 paths_ok=all(any(line.strip().startswith('Filename') and line.split(None,1)[1].replace('\\','/').lower()==plugin for line in (root/('inspect-'+name+'.log')).read_text().splitlines()) for name in ['framelinksink','framelinksrc','framelinkavsink','framelinkavsrc'])
 action='set-local-native-pool-candidate' in (root/'inspect-framelinkavsrc.log').read_text()
 return {'per_dll':per_dll,'baseline_count':len(baseline),'expected_count':len(expected),'actual_count':len(actual),'removed_baseline':sorted(baseline-actual),'removed_candidate':sorted(expected-actual),'unexpected':sorted(actual-expected),'explicit_far5_action':action,'candidate_plugin_loaded':paths_ok,'pass':all(v['pass'] for v in per_dll.values()) and actual==expected and baseline<=actual and action and paths_ok}

def collected_path(data, name):
 # Legacy Windows manifests used backslashes. Interpret both separator forms
 # explicitly; reject drives/UNC/rooted paths and traversal before resolution.
 if not isinstance(name,str) or not name or "\0" in name:
  raise RuntimeError('invalid collected manifest path')
 relative=PureWindowsPath(name)
 if relative.anchor or not relative.parts or any(part=='..' or ':' in part for part in relative.parts):
  raise RuntimeError('unsafe collected manifest path: '+name)
 root=data.resolve();path=root.joinpath(*relative.parts).resolve()
 if not path.is_relative_to(root) or path==root:
  raise RuntimeError('collected manifest path escapes root: '+name)
 return path
def verify_collection(data):
 manifest=json.loads((data/'collected-sha256.json').read_text());seen=set()
 if not isinstance(manifest,dict) or not manifest:raise RuntimeError('empty collection manifest')
 for name,digest in manifest.items():
  path=collected_path(data,name)
  if path in seen or not isinstance(digest,str) or not re.fullmatch('[a-f0-9]{64}',digest):raise RuntimeError('duplicate path or invalid collection digest')
  seen.add(path)
  if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('collected hash mismatch: '+name)
 actual={p.resolve() for p in data.rglob('*') if p.is_file() and p!=data/'collected-sha256.json'}
 if actual!=seen:raise RuntimeError('collection manifest does not cover every raw artifact')

def compatibility(root,refs):
 root=Path(root);comp=json.loads((root/'compatibility.json').read_text())
 before=json.loads((root/'candidate-binaries-before.json').read_text());after=json.loads((root/'candidate-binaries-after.json').read_text())
 dlls={'framelink.dll','framelink_gst_session.dll','gstframelink.dll'};clients=refs['baseline_clients']
 if set(before)!=dlls or before!=after or not all(isinstance(h,str) and re.fullmatch('[a-f0-9]{64}',h) for h in before.values()):raise ValueError('candidate before/after DLL pins mismatch')
 if comp.get('client_sha256')!=clients or json.loads((root/'baseline-client-hashes.json').read_text())!=clients:raise ValueError('old-client hash mismatch')
 if comp.get('candidate_dll_sha256')!={k:before[k] for k in ('framelink.dll','framelink_gst_session.dll')}:raise ValueError('compatibility loaded DLL pins mismatch')
 if set(comp.get('exits',{}))!=set(clients) or any(type(v) is not int or v!=0 for v in comp['exits'].values()) or comp.get('pass') is not True:raise ValueError('exact old-client outcomes required')
 return {'pass':True,'clients':sorted(clients),'candidate_dll_sha256':comp['candidate_dll_sha256']}

def receipt_status(root,revision,refs):
 status=json.loads((Path(root)/'status.json').read_text())
 required=['extract','configure','build-all','inventory','exports-framelink','exports-framelink_gst_session','full-ctest']
 required+=['inspect-'+n for n in ('framelinksink','framelinksrc','framelinkavsink','framelinkavsrc')]
 required+=['compat-'+action+'-'+name for name in refs['baseline_clients'] for action in ('imports','run')]
 if status.get('state')!='finished' or status.get('revision')!=revision or status.get('failures')!=[] or status.get('error'):raise ValueError('remote status/revision mismatch')
 if any(type(status.get(k+'_exit')) is not int or status[k+'_exit']!=0 for k in required):raise ValueError('required remote step did not pass')
 return status

def target_artifacts(build,target):
 """Require runtime/declared link artifacts; Release PDBs are diagnostic only."""
 kind=target['type'];expected='.exe' if kind=='EXECUTABLE' else '.dll'
 if kind not in ('EXECUTABLE','SHARED_LIBRARY','MODULE_LIBRARY'):raise ValueError('unsupported runtime target type')
 paths=[Path(build)/item['path'] for item in target.get('artifacts',[])]
 if not any(path.suffix.lower()==expected for path in paths):raise ValueError('runtime artifact undeclared: '+target['name']+' '+expected)
 optional=[p for p in paths if p.suffix.lower()=='.pdb']
 required=[p for p in paths if p.suffix.lower()!='.pdb']
 missing=[str(p) for p in required if not p.is_file()]
 return {'name':target['name'],'type':kind,'selection_policy':'runtime-and-all-declared-non-PDB-required-v1','artifacts':[str(p) for p in paths],'required':[str(p) for p in required],'optional_pdb':[str(p) for p in optional],'missing_required':missing,'missing_optional_pdb':[str(p) for p in optional if not p.is_file()],'pass':not missing}
