$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0)
if(!$locked){throw 'Runtime lane mutex held'}
try {
 $busy=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'm5_external_pipeline|rate_remote|windows-startup-phase|remote\.py')})
$busy | Select-Object ProcessId,Name,CommandLine | ConvertTo-Json | Set-Content busy-preflight.json
if($busy.Count){throw 'Runtime lane busy; nothing terminated'}

 Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json | Set-Content cpu.json
 Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory | ConvertTo-Json | Set-Content memory.json
 Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content os.json
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID | ConvertTo-Json | Set-Content gpu.json
 $archive='<win-home>/dev/framelink-m6-windows-461584b/source.tar'
 if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '24457da741884c81e2f88f154f07ae96e5c78f9d788a3799d8a3655883cb3c8a'){throw 'production source archive mismatch'}
 $manifest=Get-Content artifact-sha256.json -Raw | ConvertFrom-Json
 foreach($entry in $manifest.PSObject.Properties){
  if((Get-FileHash $entry.Name -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw ('Uploaded artifact mismatch: '+$entry.Name)}
 }
 & python -E remote.py
 $result=$LASTEXITCODE
 $remaining=@(Get-Process | Where-Object {$_.ProcessName -match '^fl_|^framelink|^device_allocation|^d3d_lifecycle'}); @{count=$remaining.Count;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content idle-final.json
 if($remaining.Count){throw 'benchmark remains live'}
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
exit $result
