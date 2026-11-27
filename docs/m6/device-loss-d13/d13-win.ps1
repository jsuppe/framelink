# Decision 13: build the device-loss injection commit (no fix) and the fix
# commit in fresh directories; run device_lost_windows at both, and the whole
# Windows suite at the fix. Under the validation mutex. Diagnostic: the fix is
# an unqualified Windows product change (requalification is decision 14).
param([string]$before, [string]$after)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(2)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk\d13"
if (Test-Path $root) { Write-Host "refusing: $root exists"; exit 1 }
New-Item -ItemType Directory $root | Out-Null
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
foreach ($pair in @(@('before', $before), @('after', $after))) {
  $name = $pair[0]; $rev = $pair[1]; $d = "$root\$name"
  git clone -q --no-checkout "$env:USERPROFILE\d13.bundle" $d; git -C $d checkout -q --detach $rev
  Write-Host ("$name revision " + (git -C $d rev-parse HEAD))
  cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { Write-Host "$name build: $_" }
  Write-Host "$name build exit $LASTEXITCODE"
  cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build -R device_lost_windows --output-on-failure 2>&1" | ForEach-Object { Write-Host "$name device_lost: $_" }
}
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $root\after && ctest --test-dir build 2>&1" | Select-String -Pattern 'tests passed|Failed|\*\*\*|Not Run|Timeout' | ForEach-Object { Write-Host "after suite: $_" }
Write-Host done
} finally { $m.ReleaseMutex() }
