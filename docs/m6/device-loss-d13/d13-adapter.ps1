# Decision 13 (A3): physical device loss on the Windows test host (Jon said
# the hosts are free). While fl_device_loss_stream_win streams frames, the
# display adapter is disabled, then re-enabled; a normal lease test then checks
# recovery. Needs an elevated session. The host falls back to the basic
# display adapter while the GPU is disabled.
param([string]$rev, [int]$repeats = 1)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(1)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$d = "$env:USERPROFILE\dev\apichk\d13\adapter-$rev"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
if (-not (Test-Path $d)) {
  git clone -q --no-checkout "$env:USERPROFILE\d13.bundle" $d; git -C $d checkout -q --detach $rev
  cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { Write-Host "build: $_" }
}
Write-Host ("revision " + (git -C $d rev-parse HEAD))
$env:PATH = "$d\build;$env:PATH"
$gpu = Get-PnpDevice -Class Display | Where-Object { $_.FriendlyName -match 'NVIDIA' } | Select-Object -First 1
if (-not $gpu) { Write-Host 'no NVIDIA adapter'; exit 1 }
Write-Host "adapter $($gpu.FriendlyName) status $($gpu.Status)"
foreach ($i in 1..$repeats) {
  $out = "$d\stream-$i.log"
  $p = Start-Process -FilePath "$d\build\fl_device_loss_stream_win.exe" -ArgumentList '40' -RedirectStandardOutput $out -NoNewWindow -PassThru
  Start-Sleep -Seconds 5
  Write-Host ("run $i disable at {0:HH:mm:ss.fff} UTC" -f (Get-Date).ToUniversalTime())
  Disable-PnpDevice -InstanceId $gpu.InstanceId -Confirm:$false
  Write-Host "run $i adapter status after disable: $((Get-PnpDevice -InstanceId $gpu.InstanceId).Status)"
  if (-not $p.WaitForExit(60000)) { Write-Host "run $i stream did not exit in 60 s"; $p.Kill() }
  Get-Content $out | ForEach-Object { Write-Host "run $i : $_" }
  Enable-PnpDevice -InstanceId $gpu.InstanceId -Confirm:$false
  Start-Sleep -Seconds 15
  Write-Host "run $i adapter status after enable: $((Get-PnpDevice -InstanceId $gpu.InstanceId).Status)"
  & "$d\build\fl_read_lease_d3d11.exe" 2>&1 | ForEach-Object { Write-Host "run $i recovery: $_" }
  Write-Host "run $i recovery exit $LASTEXITCODE"
}
Write-Host done
} finally { $m.ReleaseMutex() }
