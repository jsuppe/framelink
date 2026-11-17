from pathlib import Path,PureWindowsPath
import json,re
EXES={'fl_gst_local_native_bench_win.exe','fl_gst_native_producer_win.exe'}
DLLS={'framelink.dll','framelink_gst_session.dll','gstframelink.dll'}
NAMES=EXES|DLLS

def targets(records,binary_directory):
 result={}
 for name in NAMES:
  target=name[:-4];found=[r for r in records if r.get('name')==target]
  if len(found)!=1:raise ValueError('missing/ambiguous configured target: '+name)
  record=found[0];path=binary_directory+'/'+name
  if record.get('pass') is not True or record.get('missing_required')!=[] or PureWindowsPath(path) not in [PureWindowsPath(p) for p in record.get('required',[])]:raise ValueError('runtime artifact was not established by integrated build: '+name)
  result[name]=path
 return result

def pinset(pins,dlls):
 if set(pins)!=NAMES or any(not isinstance(h,str) or not re.fullmatch('[a-f0-9]{64}',h) for h in pins.values()):raise ValueError('five SHA256 pins required')
 if set(dlls)!=DLLS or any(pins[n]!=dlls[n] for n in DLLS):raise ValueError('recorded integrated DLL pins differ')

def analysis(result,trace):
 if result.get('pass_all') is not True or result.get('trace_checked') is not trace:raise ValueError('native analyzer did not pass expected trace arm')

def capture(c,spec,trace):
 if any(c.get(k)!=spec[k] for k in ('width','height','fps','format','seconds')) or c.get('trace') is not trace:raise ValueError('native capture profile mismatch')

def receipt(status,spec):
 if status.get('state')!='finished' or status.get('revision')!=spec['revision'] or status.get('error'):raise ValueError('remote status/revision failure')
 pins=status.get('binary_before',{});pinset(pins,spec['integrated_dll_sha256'])
 for name in ['pin-idle-before']+[arm+suffix for arm in ('trace-off','trace-on') for suffix in ('','-analysis','-idle-before','-idle-after')]:
  if type(status.get(name+'_exit')) is not int or status[name+'_exit']!=0:raise ValueError('missing successful phase: '+name)
 for arm in ('trace-off','trace-on'):
  if status.get(arm+'_binary_before')!=pins or status.get(arm+'_binary_after')!=pins:raise ValueError('binary changed during pair')
 return pins
