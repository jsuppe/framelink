$s=[datetime]'2026-11-22T20:47:40'; $e=[datetime]'2026-11-22T20:48:15'
foreach($log in 'System','Application'){ Get-WinEvent -FilterHashtable @{LogName=$log;StartTime=$s;EndTime=$e} -ErrorAction SilentlyContinue | ForEach-Object { "{0:HH:mm:ss.fff} {1} {2} {3}: {4}" -f $_.TimeCreated,$log,$_.ProviderName,$_.Id,(($_.Message -split "`n")[0]) } }
