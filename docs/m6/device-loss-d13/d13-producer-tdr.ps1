# Decision 13: a REAL GPU timeout reset in the producer on the Windows test
# host (Jon said the hosts are free). fl_device_loss_stream_win's producer
# queues a hang after HANG_AFTER frames; then a normal lease test checks
# recovery.
param([string]$rev, [int]$repeats = 3, [int]$hangAfter = 120)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(1)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$d = "$env:USERPROFILE\dev\apichk\d13\ptdr-$rev"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
if (-not (Test-Path $d)) {
  git clone -q --no-checkout "$env:USERPROFILE\d13.bundle" $d; git -C $d checkout -q --detach $rev
  cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { Write-Host "build: $_" }
}
Write-Host ("revision " + (git -C $d rev-parse HEAD))
$env:PATH = "$d\build;$env:PATH"
foreach ($i in 1..$repeats) {
  & "$d\build\fl_device_loss_stream_win.exe" 20 $hangAfter 2>&1 | ForEach-Object { Write-Host "run $i : $_" }
  Start-Sleep -Seconds 15
  & "$d\build\fl_read_lease_d3d11.exe" 2>&1 | ForEach-Object { Write-Host "run $i recovery: $_" }
  Write-Host "run $i recovery exit $LASTEXITCODE"
}
Write-Host done
} finally { $m.ReleaseMutex() }
