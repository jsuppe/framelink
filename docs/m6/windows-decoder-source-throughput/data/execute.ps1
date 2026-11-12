param([switch]$RuntimeLaneReleased)
$ErrorActionPreference='Stop'
if(!$RuntimeLaneReleased){throw 'Explicit sole runtime lane handoff required'}
Set-Location $PSScriptRoot
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0)
if(!$locked){throw 'Runtime lane held'}
function RuntimeProcesses {
 @(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild|gst-launch-1.0)\.exe$' -or ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'remote\.py|rate_remote|m5_external_pipeline')})
}
$result=1
try {
 $busy=RuntimeProcesses
 @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=$busy} | ConvertTo-Json -Depth 5 | Set-Content busy-preflight.json
 if($busy.Count){throw 'Lane busy; no processes stopped'}
 foreach($entry in (Get-Content artifact-sha256.json -Raw | ConvertFrom-Json).PSObject.Properties){if((Get-FileHash $entry.Name -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw 'Upload hash mismatch'}}
 Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json | Set-Content cpu.json
 Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content os.json
 & python -E remote.py
 $result=$LASTEXITCODE
} finally {
 $remaining=RuntimeProcesses
 @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=$remaining} | ConvertTo-Json -Depth 5 | Set-Content idle-final.json
 $mutex.ReleaseMutex();$mutex.Dispose()
}
if($remaining.Count){throw 'Runtime still active; no unrelated process stopped'}
exit $result
