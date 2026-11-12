$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$m=[System.Threading.Mutex]::new($false,'Local\FrameLinkFocusedWindowsValidation')
if(-not $m.WaitOne(0)){throw 'runtime lane held'}
try {
 $p=@(Get-Process | Where-Object {$_.ProcessName -match '^fl_|^framelink|^vkrenderer|^ctest|^cmake|^MSBuild|^device_allocation'})
 if($p.Count){throw 'lane busy; nothing terminated'}
 & python -E remote.py
 $rc=$LASTEXITCODE
} finally {$m.ReleaseMutex();$m.Dispose()}
exit $rc
