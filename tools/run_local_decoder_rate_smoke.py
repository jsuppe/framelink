#!/usr/bin/env python3
"""Bounded CTest decoder-rate smoke; preserve every result outside source tree."""
import argparse,json,pathlib,subprocess,tempfile
from check_local_decoder_rate import analyze
p=argparse.ArgumentParser();p.add_argument('exe');p.add_argument('plugin');a=p.parse_args()
root=pathlib.Path(tempfile.mkdtemp(prefix='framelink-decoder-rate-'))
print('Evidence:',root,flush=True)
with (root/'run.log').open('w') as log:
    rc=subprocess.run([a.exe,a.plugin,'--out',str(root/'capture'),'--seconds','2'],stdout=log,stderr=subprocess.STDOUT,timeout=75).returncode
if rc:raise SystemExit(rc)
try:result=analyze(root/'capture')
except (ValueError,KeyError,TypeError,OSError,ZeroDivisionError) as error:result={'pass':False,'error':str(error)}
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['pass'] else 1)
