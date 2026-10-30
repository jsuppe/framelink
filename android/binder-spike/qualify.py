#!/usr/bin/env python3
"""Opt-in isolated Binder spike; clears device logcat. Requires differently signed impostor."""
import argparse,pathlib,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--output',required=True);a=p.parse_args()
r=pathlib.Path(__file__).resolve().parent;out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
def adb(*args):
 result=subprocess.run(['adb','-s',a.serial,*args],text=True,capture_output=True,timeout=30)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 return result.stdout
checks=[('revoke','','client','revoked_eof=true'),('owner-death','death','client','transfer result=0'),('owner-token-death','owner-token-death','client','owner_death_eof=true'),('replacement','replace','client','replacement_safe=true'),('wrong-signer','','impostor','permission_rejected=true')]
results=[]
try:
 for role in ['broker','client','impostor']:
  adb('install','-r',str(r/role/'build/outputs/apk/debug'/f'{role}-debug.apk'))
 for name,mode,package,expected in checks:
  for role in ['client','impostor']:adb('shell','am','force-stop','dev.framelink.spike.'+role)
  adb('logcat','-c')
  adb('shell','am','start','-n',f'dev.framelink.spike.{package}/dev.framelink.spike.Client',*(['--es','mode',mode] if mode else []))
  deadline=time.monotonic()+8
  while True:
   log=adb('logcat','-d','-s','FLBroker:I','FLClient:I','AndroidRuntime:E')
   if expected in log or time.monotonic()>deadline:break
   time.sleep(.2)
  ok=expected in log and 'FATAL EXCEPTION' not in log
  if name=='wrong-signer':ok=ok and 'grant uid=' not in log
  else:ok=ok and 'pixel_roundtrip=true' in log
  if name=='owner-token-death':
   time.sleep(.2);log=adb('logcat','-d','-s','FLBroker:I','FLClient:I','AndroidRuntime:E');ok=ok and 'reason=owner-death' in log
  (out/(name+'.log')).write_text(log);results.append((name,ok));print(name,ok,flush=True)
 (out/'selinux.txt').write_text(adb('shell','getenforce'))
finally:
 for role in ['client','impostor','broker']:adb('shell','am','force-stop','dev.framelink.spike.'+role)
(out/'summary.txt').write_text('\n'.join(f'{name}: {ok}' for name,ok in results)+'\n')
raise SystemExit(0 if all(ok for _,ok in results) else 1)
