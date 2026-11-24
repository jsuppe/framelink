powercfg /q SCHEME_CURRENT SUB_VIDEO VIDEOIDLE | Select-String 'Current AC'
powercfg /q SCHEME_CURRENT SUB_VIDEO VIDEOCONLOCK | Select-String 'Current AC'
Get-CimInstance -Namespace root\wmi WmiMonitorID | ForEach-Object { 'monitor: ' + (-join [char[]]($_.UserFriendlyName | Where-Object { $_ -ne 0 })) + ' active=' + $_.Active }
Get-CimInstance -Namespace root\wmi WmiMonitorConnectionParams -ErrorAction SilentlyContinue | ForEach-Object { 'conn: ' + $_.InstanceName + ' type=' + $_.VideoOutputTechnology }
$since = (Get-Date).Date.AddHours(17).AddMinutes(30)
Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Power|Display|nvlddmkm|Monitor|Dwm|PnP|Win32k' } | Sort-Object TimeCreated | Select-Object -First 60 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss.fff') + ' ' + $_.ProviderName + ' ' + $_.Id + ' ' + (($_.Message -split "`n")[0]) }
'--- user32/kernel-power idle display events (Microsoft-Windows-Kernel-Power 566 / Diagnostics-Performance) ---'
Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-Kernel-Power/Thermal-Operational'; StartTime=$since} -ErrorAction SilentlyContinue | Select-Object -First 5 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss') + ' ' + $_.Id }
