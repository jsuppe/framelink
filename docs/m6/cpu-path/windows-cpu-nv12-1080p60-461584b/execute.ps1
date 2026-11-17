$ErrorActionPreference='Stop'
Set-Location '<win-home>/dev/framelink-cpu-nv12-1080p60-461584b'
$busy=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'm5_external_pipeline|rate_remote|windows-startup-phase')})
$busy | Select-Object ProcessId,Name,CommandLine | ConvertTo-Json | Set-Content busy-preflight.json
if($busy.Count){throw 'Runtime lane busy; nothing terminated'}
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0)
if(!$locked){throw 'Runtime lane mutex held'}
try {
 Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content os.json
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID | ConvertTo-Json | Set-Content gpu.json
 $archive='<win-home>/dev/framelink-m6-windows-461584b/source.tar'
 if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '24457da741884c81e2f88f154f07ae96e5c78f9d788a3799d8a3655883cb3c8a'){throw 'production source archive mismatch'}
 & python remote.py
 $result=$LASTEXITCODE
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
exit $result
