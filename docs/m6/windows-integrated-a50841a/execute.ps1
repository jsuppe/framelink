$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0)
if(!$locked){throw 'Windows runtime lane is held'}
$result=1
try {
 $busy=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'remote\.py|m5_external_pipeline|rate_remote')})
 New-Item -ItemType Directory host | Out-Null
 @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=@($busy | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5 | Set-Content host/idle-before.json
 if($busy.Count){throw 'Busy runtime; no unrelated process terminated'}
 $manifest=Get-Content artifact-sha256.json -Raw | ConvertFrom-Json
 foreach($entry in $manifest.PSObject.Properties){if((Get-FileHash $entry.Name -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw ('Artifact hash mismatch: '+$entry.Name)}}
 Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content host/os.json
 Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json | Set-Content host/cpu.json
 Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory | ConvertTo-Json | Set-Content host/memory.json
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json | Set-Content host/gpu.json
 & python -E remote.py
 $result=$LASTEXITCODE
} finally {
 if(Test-Path host){
  $remaining=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$'})
  @{utc=[DateTime]::UtcNow.ToString('o');activeRuntime=@($remaining | Select-Object ProcessId,Name,CommandLine)} | ConvertTo-Json -Depth 5 | Set-Content host/idle-final.json
  if($remaining.Count){$result=1}
 }
 $mutex.ReleaseMutex();$mutex.Dispose()
}
exit $result
