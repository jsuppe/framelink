from pathlib import Path
import subprocess,json,time,base64,hashlib
repo=Path('<linux-home>/dev/framelink-m6');root=Path('<linux-home>/dev/evidence/m6-lifecycle/lease-cycles');root.mkdir(exist_ok=True)
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];device='/data/local/tmp/framelink-m6-20261106';remote='<win-home>/dev/framelink-m6-20261106'
s={'state':'running','started':time.time(),'source_sha256':hashlib.sha256((repo/'test/fl_read_leases.cpp').read_bytes()).hexdigest(),'cases':[]}
def save():(root/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,label,timeout=600):
 with (root/(label+'.log')).open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 assert r.returncode==0,label+' execution failed '+str(r.returncode)
def analyze(label):
 lines=(root/(label+'.log')).read_text().splitlines();rows=[tuple(map(int,l.split('=',1)[1].split('|'))) for l in lines if l.startswith('cycle.')]
 assert len(rows)==100,(label,len(rows))
 def slope(j):
  pairs=list(enumerate(rows))[5:];n=len(pairs);sx=sum(x for x,y in pairs);sy=sum(y[j] for x,y in pairs)
  return (n*sum(x*y[j] for x,y in pairs)-sx*sy)/(n*sum(x*x for x,y in pairs)-sx*sx)
 slopes=[slope(j) for j in range(3)];ok=all(x<=limit for x,limit in zip(slopes,[16,.1,.02]))
 s['cases'].append({'label':label,'cycles':100,'slopes_memory_kb_handles_threads':slopes,'accepted':ok,'first_warm':rows[5],'last':rows[-1]});save();assert ok,label+' resource gate failed'
def ps(code):return ['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()]
save()
try:
 run(['<linux-home>/dev/build/framelink-m6-linux/fl_read_leases','--cycles','100'],'linux');analyze('linux')
 run(['cmake','--build','<linux-home>/dev/build/framelink-m6-arm64','--target','fl_read_leases','-j','4'],'android-build')
 subprocess.run(adb+['push','<linux-home>/dev/build/framelink-m6-arm64/fl_read_leases',device+'/fl_read_leases'],check=True,stdout=subprocess.DEVNULL)
 run(adb+['shell','cd '+device+' && LD_LIBRARY_PATH='+device+' ./fl_read_leases --cycles 100'],'android');analyze('android')
 old=root/'windows.before.cpp';subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/test/fl_read_leases.cpp',str(old)],check=True)
 assert old.read_bytes().replace(b'\r\n',b'\n')==subprocess.check_output(['git','show','HEAD:test/fl_read_leases.cpp'],cwd=repo).replace(b'\r\n',b'\n'),'Windows source drift'
 subprocess.run(['scp','-q',str(repo/'test/fl_read_leases.cpp'),'<win-user>@<win-host>:'+remote+'/test/fl_read_leases.cpp'],check=True)
 prefix="$ErrorActionPreference='Stop'\nSet-Location '"+remote+"'\n"
 run(ps(prefix+'cmake --build build --config Release --target fl_read_leases -j 4\nexit $LASTEXITCODE'),'windows-build')
 run(ps(prefix+'& ./build/Release/fl_read_leases.exe --cycles 100\nexit $LASTEXITCODE'),'windows');analyze('windows')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s),flush=True);raise SystemExit(0 if s['state']=='finished' else 1)
