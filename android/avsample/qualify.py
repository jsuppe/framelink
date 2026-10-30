#!/usr/bin/env python3
"""Opt-in device test for the isolated dev.framelink.av qualification APK.
Clears logcat to collect each isolated case; never modifies renderer APKs.
"""
import argparse, pathlib, subprocess, time
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--output',required=True);a=p.parse_args()
out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
def adb(*args):
 r=subprocess.run(['adb','-s',a.serial,*args],text=True,capture_output=True,timeout=30)
 if r.returncode:raise RuntimeError(r.stderr+r.stdout)
 return r.stdout
results=[]
try:
 for case in ['normal','background','focus','reopen','surface-recreate']:
  adb('shell','am','force-stop','dev.framelink.av');adb('logcat','-c')
  adb('shell','am','start','-n','dev.framelink.av/.MainActivity');time.sleep(1.3)
  if case=='background':adb('shell','input','keyevent','3')
  if case=='surface-recreate':adb('shell','am','start','-f','0x20000000','-n','dev.framelink.av/.MainActivity','--ez','recreate','true')
  if case=='focus':adb('shell','am','startservice','-n','dev.framelink.av/.FocusTestService')
  time.sleep(7 if case=='surface-recreate' else 4)
  log=adb('logcat','-d','-s','FrameLinkAV:I','AndroidRuntime:E');(out/(case+'.log')).write_text(log)
  if case in ['normal','reopen','surface-recreate']:ok='receiver result=1' in log and 'audio_samples=144000' in log and 'producer result=0' in log
  else:ok='clock audio_master=1' in log and 'playback cancelled' in log
  if case=='focus':ok=ok and 'focus=-2' in log and 'test competing focus=1' in log
  results.append((case,ok));print(case,'PASS' if ok else 'FAIL',flush=True)
finally:adb('shell','am','force-stop','dev.framelink.av')
(out/'summary.txt').write_text('\n'.join(f'{name}: {ok}' for name,ok in results)+'\n')
raise SystemExit(0 if all(ok for _,ok in results) else 1)
