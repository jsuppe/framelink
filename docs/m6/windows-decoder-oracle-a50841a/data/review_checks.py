"""Local-only receipt checks. Never execute network or Windows commands."""
from pathlib import Path
import json,hashlib,math
import integrated_verify as integrated

def validate_prior(prior,revision,expected_refs):
 prior=Path(prior);controller=json.loads((prior/'controller-status.json').read_text())
 if controller.get('state')!='finished' or controller.get('revision')!=revision or controller.get('local_checks_pass') is not True or controller.get('error') or controller.get('collection_error') or any(type(controller.get(k)) is not int or controller[k]!=0 for k in ('remote_exit','package_exit','pull_exit')):raise ValueError('successful exact integrated run prerequisite missing')
 data=prior/'data';integrated.verify_collection(data)
 for name in ('config.json','references.json','artifact-sha256.json','remote.py','execute.ps1','collect.py','verify.py'):
  if (data/name).read_bytes()!=(prior/name).read_bytes():raise ValueError('prior uploaded artifact identity mismatch: '+name)
 config=json.loads((data/'config.json').read_text())
 if config.get('revision')!=revision:raise ValueError('prior source revision mismatch')
 if config.get('archive_sha256')!=hashlib.sha256((prior/'source.tar').read_bytes()).hexdigest():raise ValueError('prior archive hash mismatch')
 refs=json.loads((data/'references.json').read_text())
 if refs!=expected_refs:raise ValueError('prior API/client reference set differs from reviewed references')
 results=data/'results';status=integrated.receipt_status(results,revision,refs)
 coverage=integrated.ctest(results/'inventory.log',results/'full.junit.xml')
 if not coverage['pass'] or status.get('expected_tests')!=coverage['expected_count']:raise ValueError('prior CTest coverage failed')
 if not integrated.api(results,refs)['pass'] or not integrated.compatibility(results,refs)['pass']:raise ValueError('prior API/compatibility failed')
 targets=json.loads((results/'configured-targets.json').read_text())
 if not targets:raise ValueError('prior configured target proof absent')
 for t in targets:
  expected='.exe' if t.get('type')=='EXECUTABLE' else '.dll'
  if t.get('type') not in ('EXECUTABLE','SHARED_LIBRARY','MODULE_LIBRARY') or t.get('selection_policy')!='runtime-and-all-declared-non-PDB-required-v1' or t.get('pass') is not True or t.get('missing_required')!=[] or not any(str(p).lower().endswith(expected) for p in t.get('required',[])):raise ValueError('prior runtime target artifacts incomplete')
 if json.loads((data/'host/idle-final.json').read_text(encoding='utf-8-sig')).get('activeRuntime')!=[]:raise ValueError('prior final idle missing/busy')
 return {'pass':True,'revision':revision,'tests':coverage['expected_count']}

def phase_receipt(result):
 if result.get('frames')!=150 or set(result.get('phases',{}))!={'map','sha','unmap'}:raise ValueError('phase output missing/count mismatch')
 for phase in result['phases'].values():
  for key in ('wall_ms_median','wall_ms_max','wall_ms_sum','thread_cpu_ms_median','thread_cpu_ms_sum'):
   v=phase.get(key)
   if not isinstance(v,(int,float)) or not math.isfinite(v) or v<0:raise ValueError('invalid phase result')
 return result
