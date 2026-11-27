# Decision 13: a REAL GPU timeout reset on the Windows test host (Jon said the
# hosts are free). Builds the revision, records the TDR settings, runs
# fl_read_lease_d3d11 --gpu-hang-consumer REPEATS times with a pause for the
# display to recover, and collects the display driver's recovery events.
param([string]$rev, [int]$repeats = 3)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(1)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$d = "$env:USERPROFILE\dev\apichk\d13\tdr"
if (Test-Path $d) { Write-Host "refusing: $d exists"; exit 1 }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
git clone -q --no-checkout "$env:USERPROFILE\d13.bundle" $d; git -C $d checkout -q --detach $rev
Write-Host ("revision " + (git -C $d rev-parse HEAD))
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { Write-Host "build: $_" }
Write-Host "build exit $LASTEXITCODE"
$key = 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers'
foreach ($n in 'TdrLevel','TdrDelay','TdrDdiDelay','TdrLimitCount','TdrLimitTime') { $v = (Get-ItemProperty $key -Name $n -ErrorAction SilentlyContinue).$n; Write-Host "registry $n = $(if ($null -eq $v) {'(default)'} else {$v})" }
$since = Get-Date
$env:PATH = "$d\build;$env:PATH"
foreach ($i in 1..$repeats) {
  $t = Get-Date
  & "$d\build\fl_read_lease_d3d11.exe" --gpu-hang-consumer 2>&1 | ForEach-Object { Write-Host "run $i : $_" }
  Write-Host ("run $i exit $LASTEXITCODE, {0:N1} s" -f ((Get-Date) - $t).TotalSeconds)
  Start-Sleep -Seconds 20
}
Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Display|nvlddmkm|dxgkrnl' -or $_.Id -eq 4101 } | ForEach-Object { Write-Host ("event {0:o} {1} {2}: {3}" -f $_.TimeCreated.ToUniversalTime(), $_.ProviderName, $_.Id, ($_.Message -replace '\s+',' ').Substring(0,[Math]::Min(200,($_.Message -replace '\s+',' ').Length))) }
Write-Host done
} finally { $m.ReleaseMutex() }
