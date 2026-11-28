# Decision 13: a REAL whole-device reset on the Windows test host (Jon said
# the hosts are free): DXCap -forcetdr while fl_device_loss_stream_win streams
# frames; then a normal lease test checks recovery.
param([string]$rev, [int]$repeats = 3)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(1)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$d = "$env:USERPROFILE\dev\apichk\d13\ptdr-$rev"
if (-not (Test-Path "$d\build\fl_device_loss_stream_win.exe")) { Write-Host "missing build $d"; exit 1 }
Write-Host ("revision " + (git -C $d rev-parse HEAD))
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$d\build;$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
$since = Get-Date
foreach ($i in 1..$repeats) {
  $out = "$d\forcetdr-$i.log"
  $p = Start-Process -FilePath "$d\build\fl_device_loss_stream_win.exe" -ArgumentList '30' -RedirectStandardOutput $out -NoNewWindow -PassThru
  Start-Sleep -Seconds 5
  Write-Host ("run $i forcetdr at {0:HH:mm:ss.fff} UTC" -f (Get-Date).ToUniversalTime())
  & DXCap.exe -forcetdr 2>&1 | ForEach-Object { Write-Host "run $i dxcap: $_" }
  if (-not $p.WaitForExit(90000)) { Write-Host "run $i stream did not exit in 90 s"; $p.Kill() }
  Get-Content $out | Where-Object { $_ -notmatch '\(still\)' } | ForEach-Object { Write-Host "run $i : $_" }
  Start-Sleep -Seconds 15
  & "$d\build\fl_read_lease_d3d11.exe" 2>&1 | ForEach-Object { Write-Host "run $i recovery: $_" }
  Write-Host "run $i recovery exit $LASTEXITCODE"
}
Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Display|nvlddmkm' -or $_.Id -eq 4101 } | ForEach-Object { Write-Host ("event {0:o} {1} {2}" -f $_.TimeCreated.ToUniversalTime(), $_.ProviderName, $_.Id) }
Write-Host done
} finally { $m.ReleaseMutex() }
