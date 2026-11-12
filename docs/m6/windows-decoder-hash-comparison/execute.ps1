$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0)
if(!$locked){throw 'Exclusive Windows lane is held'}
try {
 $busy=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'm5_external_pipeline|rate_remote|windows-startup-phase|remote\.py')})
 $busy | Select-Object ProcessId,Name,CommandLine | ConvertTo-Json | Set-Content busy-preflight.json
 if($busy.Count){throw 'Windows lane busy; no process terminated'}
 $manifest=Get-Content artifact-sha256.json -Raw | ConvertFrom-Json
 foreach($entry in $manifest.PSObject.Properties){if((Get-FileHash $entry.Name -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw ('Artifact hash mismatch: '+$entry.Name)}}
 Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content os.json
 Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json | Set-Content cpu.json
 Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory | ConvertTo-Json | Set-Content memory.json
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json | Set-Content gpu.json
 & python -E remote.py
 $result=$LASTEXITCODE
 $remaining=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$'})
 @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=@($remaining | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 idle-final.json
 if($remaining.Count){throw 'Runtime still active after bounded run; no unrelated process terminated'}
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
exit $result
