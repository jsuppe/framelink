#!/usr/bin/env python3
"""CTest-owned short fixture run; its failed evidence is deliberately retained."""
import argparse,pathlib,subprocess,tempfile
from check_local_native_bench import analyze
p=argparse.ArgumentParser();p.add_argument('exe');p.add_argument('plugin');a=p.parse_args()
root=pathlib.Path(tempfile.mkdtemp(prefix='framelink-native-bench-'))
print('Evidence:',root,flush=True)
# Root evidence directory survives both failure and success; executable requires absent capture.
with (root/'run.log').open('w') as f:
 rc=subprocess.run([a.exe,a.plugin,'--out',str(root/'capture'),'--seconds','2'],stdout=f,stderr=subprocess.STDOUT,timeout=75).returncode
if rc:raise SystemExit(rc)
import json
r=analyze(root/'capture');(root/'analysis.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
