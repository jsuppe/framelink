from pathlib import Path
import subprocess,base64,json,datetime
root=Path(__file__).parent; remote='<win-home>/dev/device-settle-batches-20261112'
def ps(code,timeout=300):
 p=subprocess.run(['ssh','-o','BatchMode=yes','-o','ServerAliveInterval=15','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout);return p
p=ps("$p=@(Get-Process | Where-Object {$_.ProcessName -match 'fl_gst|d3d_lifecycle|device_allocation|python'}); $p | Select-Object ProcessName,Id | ConvertTo-Json; if($p.Count){exit 9}; New-Item -ItemType Directory -Force '"+remote+"' | Out-Null")
(root/'idle-before.log').write_bytes(p.stdout);assert p.returncode==0,p.stdout
for n in ['probe.cpp','CMakeLists.txt']:subprocess.run(['scp','-q',str(root/n),'<win-user>@<win-host>:'+remote+'/'+n],check=True)
code=r'''$ErrorActionPreference='Stop'
$m=[System.Threading.Mutex]::new($false,'Local\FrameLinkFocusedWindowsValidation')
if(-not $m.WaitOne(0)){throw 'runtime lane held'}
try {
Set-Location 'REMOTE'
$env:MSBUILDDISABLENODEREUSE='1'
cmake -S . -B build -A x64
if($LASTEXITCODE){throw 'configure'}
cmake --build build --config Release -- /nr:false
if($LASTEXITCODE){throw 'build'}
Get-FileHash build/Release/device_allocation_isolation.exe | ConvertTo-Json | Set-Content before.json
foreach($mode in @('hardware')){
 $a=@('hardware','empty')
 $name=$a[0]+'-'+$a[1]
 & ./build/Release/device_allocation_isolation.exe $a[0] $a[1] > ($name+'.csv') 2> ($name+'.stderr')
 if($LASTEXITCODE){throw ('probe failed '+$name)}
}
Get-FileHash build/Release/device_allocation_isolation.exe | ConvertTo-Json | Set-Content after.json
@{state='finished';utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content status.json
} finally {$m.ReleaseMutex();$m.Dispose()}
'''.replace('REMOTE',remote)
(root/'execute.ps1').write_text(code)
s={'state':'running','started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'owner':'qualification_recovery','remote':remote,'cases':'one hardware-empty process, three batches of100 cycles with30seconds settle each; not production acceptance'};(root/'controller.json').write_text(json.dumps(s,indent=2));print(json.dumps(s),flush=True)
p=ps(code,600);(root/'run.log').write_bytes(p.stdout);s['exit']=p.returncode
subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/*.csv',str(root)],check=True)
subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/*.json',str(root)],check=True)
p2=ps("@(Get-Process | Where-Object {$_.ProcessName -match 'fl_gst|d3d_lifecycle|device_allocation|python'}) | Select-Object ProcessName,Id | ConvertTo-Json")
(root/'idle-after.log').write_bytes(p2.stdout);s.update(state='finished' if p.returncode==0 else 'failed',ended=datetime.datetime.now(datetime.timezone.utc).isoformat());(root/'controller.json').write_text(json.dumps(s,indent=2));print(json.dumps(s),flush=True);raise SystemExit(p.returncode)
