# Full Windows suite (serial ctest) at c485ea44: master 0d72a94b (drain-report fix, PR #46) plus PR #47.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '%USERPROFILE%\dev\apichk'; $d = "$root\suite-c485ea4"
if (Test-Path $d) { Write-Host "refusing: $d exists"; exit 1 }
git clone -q --no-checkout "$env:USERPROFILE\m6s.bundle" $d; git -C $d checkout -q --detach c485ea44
Write-Host ("revision " + (git -C $d rev-parse HEAD))
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10
Write-Host "build exit $LASTEXITCODE"
Set-Location $d; $env:PATH = "$d\build;$env:PATH"
$t0 = Get-Date
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build --output-on-failure > $root\suite-c485ea4.log 2>&1"
Write-Host "ctest exit $LASTEXITCODE, $([int]((Get-Date)-$t0).TotalSeconds) s"
Get-Content "$root\suite-c485ea4.log" | Select-String -Pattern 'tests passed|\*\*\*Failed|Not Run|Timeout|Skipped|Total Test time' | ForEach-Object { Write-Host "$_" }
} finally { $m.ReleaseMutex() }
