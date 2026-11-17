$ErrorActionPreference='Stop'
$busy=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'framelink-4k|m5_external_pipeline|rate_remote|remote\.py')})
$result=@{
 utc=[DateTime]::UtcNow.ToString('o')
 cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed)
 computer=Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer,Model,TotalPhysicalMemory,NumberOfProcessors,NumberOfLogicalProcessors
 os=Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture
 gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID)
 activeRuntime=@($busy | Select-Object ProcessId,Name,CommandLine)
 binaryHashes=@(Get-FileHash <win-home>/dev/framelink-m6-windows-461584b/build/Release/fl_gst_av_bench.exe,<win-home>/dev/framelink-m6-windows-461584b/build/Release/gstframelink.dll,<win-home>/dev/framelink-m6-windows-461584b/build/Release/framelink_gst_session.dll,<win-home>/dev/framelink-m6-windows-461584b/build/Release/framelink.dll -Algorithm SHA256 | Select-Object Path,Hash)
}
$result | ConvertTo-Json -Depth 6 | Set-Content ($PSScriptRoot+'/host-final.json')
$result | ConvertTo-Json -Depth 6
if($busy.Count){exit 1}
