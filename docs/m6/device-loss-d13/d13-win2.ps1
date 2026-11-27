# Decision 13: separate regressions from existing failures. Runs the four
# tests that failed in the fix's Windows suite on the injection-only build
# ("before", already built by d13-win.ps1), builds the guarded fix, runs the
# four there, then the whole suite. Under the validation mutex.
param([string]$rev, [int]$repeats = 3)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(2)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk\d13"; $d = "$root\fix2"
if (Test-Path $d) { Write-Host "refusing: $d exists"; exit 1 }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
$four = '^(read_lease_d3d11_producer_death|gst_relay_downstream_death_win|gst_native_source_win|gst_av_bench_fault_producer_kill)$'
git clone -q --no-checkout "$env:USERPROFILE\d13.bundle" $d; git -C $d checkout -q --detach $rev
Write-Host ("fix2 revision " + (git -C $d rev-parse HEAD))
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { Write-Host "fix2 build: $_" }
Write-Host "fix2 build exit $LASTEXITCODE"
foreach ($name in 'before', 'fix2') {
  foreach ($i in 1..$repeats) {
    cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $root\$name && ctest --test-dir build -R `"$four`" --output-on-failure 2>&1" | Select-String -Pattern 'Test +#|tests passed|line [0-9]|error|FAIL' | ForEach-Object { Write-Host "$name run $i : $_" }
  }
}
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build 2>&1" | Select-String -Pattern 'tests passed|\*\*\*' | ForEach-Object { Write-Host "fix2 suite: $_" }
Write-Host done
} finally { $m.ReleaseMutex() }
