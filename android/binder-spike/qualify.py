#!/usr/bin/env python3
"""Opt-in isolated Binder spike; uses per-case log markers without clearing logcat. Requires differently signed impostor."""
import argparse,pathlib,subprocess,time,uuid
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--output',required=True);a=p.parse_args()
r=pathlib.Path(__file__).resolve().parent;out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
def adb(*args):
 result=subprocess.run(['adb','-s',a.serial,*args],text=True,capture_output=True,timeout=30)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 return result.stdout
checks=[('stream-broker-death','streams-broker-death','client','stream_broker_death=true'),('stream-process-death','streams-process-death','client','stream_peer_eof=true'),('stream-wrong-signer','streams','impostor','permission_rejected=true'),('stream-owner-death','streams-owner-death','client','stream_owner_death=true'),('stream-pair','streams','client','stream_pair_safe=true'),('revoke','','client','revoked_eof=true'),('owner-death','death','client','transfer result=0'),('owner-token-death','owner-token-death','client','owner_death_eof=true'),('replacement','replace','client','replacement_safe=true'),('wrong-signer','','impostor','permission_rejected=true')]
results=[]
try:
 for role in ['broker','client','impostor']:
  adb('install','-r',str(r/role/'build/outputs/apk/debug'/f'{role}-debug.apk'))
 for name,mode,package,expected in checks:
  for role in ['client','impostor']:adb('shell','am','force-stop','dev.framelink.spike.'+role)
  marker='fl-qualify-'+uuid.uuid4().hex
  adb('shell','log','-t','FLQualify',marker)
  def case_log():
   log=adb('logcat','-d','-s','FLQualify:I','FLBroker:I','FLClient:I','AndroidRuntime:E')
   if marker not in log:raise RuntimeError('case marker missing; log window cannot be trusted')
   return log.split(marker,1)[1]
  killed=False
  adb('shell','am','start','-n',f'dev.framelink.spike.{package}/dev.framelink.spike.Client',*(['--es','mode',mode] if mode else []))
  deadline=time.monotonic()+8
  while True:
   log=case_log()
   if name=='stream-broker-death' and 'stream_ready=true' in log and not killed:
    adb('shell','am','force-stop','dev.framelink.spike.broker');killed=True
   complete=expected in log
   if name=='stream-process-death':complete=complete and ('stream_revoke=owner-death' in log or 'stream_revoke=transport-end' in log)
   if name=='stream-owner-death':complete=complete and 'stream_revoke=owner-death' in log
   if complete or time.monotonic()>deadline:break
   time.sleep(.2)
  ok=expected in log and 'FATAL EXCEPTION' not in log
  if name.endswith('wrong-signer'):ok=ok and 'grant uid=' not in log
  elif not name.startswith('stream-'):ok=ok and 'pixel_roundtrip=true' in log
  if name=='stream-process-death':ok=ok and 'stream_roles=true' in log and ('stream_revoke=owner-death' in log or 'stream_revoke=transport-end' in log)
  if name=='stream-owner-death':ok=ok and 'stream_revoke=owner-death' in log
  if name=='owner-token-death':
   time.sleep(.2);log=case_log();ok=ok and 'reason=owner-death' in log
  (out/(name+'.log')).write_text(log);results.append((name,ok));print(name,ok,flush=True)
 (out/'selinux.txt').write_text(adb('shell','getenforce'))
finally:
 for role in ['client','impostor','broker']:adb('shell','am','force-stop','dev.framelink.spike.'+role)
(out/'summary.txt').write_text('\n'.join(f'{name}: {ok}' for name,ok in results)+'\n')
raise SystemExit(0 if all(ok for _,ok in results) else 1)
