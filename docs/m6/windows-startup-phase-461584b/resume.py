from pathlib import Path
import subprocess,json,base64,datetime
root=Path(__file__).parent;run=root/'runs/20261112T062145Z-e9d244c0';remote=json.loads((run/'status.json').read_text())['remote']
old=(run/'execute.ps1').read_text();start=old.index(' Get-ChildItem Env:');end=old.index(" $state.status='finished'",start);arms=old[start:end]
code="""$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
function Assert-Idle {
 $active=@(Get-CimInstance Win32_Process | Where-Object {
  $_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\\.exe$' -or
  ($_.Name -match '^python(w|3)?\\.exe$' -and $_.CommandLine -match 'm5_external_pipeline')
 })
 if($active.Count){$active | Select-Object ProcessId,Name,CommandLine | Format-List;throw 'Runtime busy'}
}
"""+f"Set-Location '{remote}'\n"+"""Assert-Idle
if((Test-Path results/audio) -or (Test-Path results/audio-coordinated)){throw 'Media already attempted'}
foreach($row in (Get-Content results/binaries.json -Raw | ConvertFrom-Json)){
 if((Get-FileHash $row.Path -Algorithm SHA256).Hash -ne $row.Hash){throw 'Binary changed'}
}
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0);if(!$locked){throw 'Lane held'}
Copy-Item results/status.json results/status-pre-resume.json
$state=@{revision='461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff';status='resuming-verified-binaries';scope='diagnostic, not acceptance'}
function Save-State {$state | ConvertTo-Json -Depth 5 | Set-Content results/status.json}
$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
try {
"""+arms+"""
 $state.status='finished'
} catch {$state.status='failed';$state.error=$_.Exception.Message}
finally {$state.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-State;if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
if($state.status -ne 'finished'){exit 1}
exit 0
"""
script=run/'resume.ps1';script.write_text(code)
subprocess.run(['scp','-q',str(script),'<win-user>@<win-host>:'+remote+'/resume.ps1'],check=True)
state={'remote':remote,'state':'running-resume','started':datetime.datetime.now(datetime.timezone.utc).isoformat()};(run/'resume-status.json').write_text(json.dumps(state,indent=2))
with (run/'resume.log').open('w') as f:
 state['exit']=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote+'/resume.ps1'],stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
out=run/'resumed-results';out.mkdir(exist_ok=False)
state['pull_exit']=subprocess.run(['scp','-rq','<win-user>@<win-host>:'+remote+'/results',str(out)],timeout=120).returncode
state['analysis_exit']=subprocess.run(['python3',str(root/'analyze.py'),str(out/'results')],timeout=60).returncode
state['state']='finished';(run/'resume-status.json').write_text(json.dumps(state,indent=2));print(json.dumps(state,indent=2))
