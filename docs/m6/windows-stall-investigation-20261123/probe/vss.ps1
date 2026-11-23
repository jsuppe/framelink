$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
 $d = '<win-home>\dev\apichk\stallprobe'; $out = "$d\vss-test"
 if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
 $before = @(Get-CimInstance Win32_ShadowCopy | ForEach-Object ID)
 Write-Host "shadow copies before: $($before.Count)"
 $p = Start-Process -FilePath "$d\stallprobe.exe" -ArgumentList @($out,'300','--wpr','"-start CPU -start DiskIO -start FileIO"','--trigger-ms','50','--max-saves','2','--log-ms','5') -RedirectStandardOutput "$d\vss-test.log" -NoNewWindow -PassThru
 $made = @()
 foreach ($at in 60,180) {
  while ((Get-Date) -lt $p.StartTime.AddSeconds($at)) { Start-Sleep -Milliseconds 200 }
  $t0 = Get-Date
  $r = Invoke-CimMethod -ClassName Win32_ShadowCopy -MethodName Create -Arguments @{Volume='C:\'; Context='ClientAccessible'}
  $t1 = Get-Date
  Write-Host ("create at {0:HH:mm:ss.fff} returned {1:HH:mm:ss.fff} ({2:N0} ms) rc={3} id={4}" -f $t0,$t1,($t1-$t0).TotalMilliseconds,$r.ReturnValue,$r.ShadowID)
  if ($r.ReturnValue -eq 0) { $made += $r.ShadowID }
  while ((Get-Date) -lt $p.StartTime.AddSeconds($at+60)) { Start-Sleep -Milliseconds 200 }
  foreach ($id in $made) { Get-CimInstance Win32_ShadowCopy | Where-Object ID -eq $id | Remove-CimInstance; Write-Host ("deleted {0} at {1:HH:mm:ss}" -f $id,(Get-Date)) }
  $made = @()
 }
 $p.WaitForExit()
 Get-Content "$d\vss-test.log"
 $after = @(Get-CimInstance Win32_ShadowCopy | ForEach-Object ID)
 Write-Host "shadow copies after: $($after.Count) (left over from this test: $(@($after | Where-Object { $before -notcontains $_ }).Count))"
 Get-Content "$out\events.csv"
} finally { $m.ReleaseMutex() }
