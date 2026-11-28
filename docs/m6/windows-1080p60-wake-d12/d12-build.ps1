# Decision 12 A2: build the diagnostic harness (test-only pacing record) and the
# probe in a fresh directory. Diagnostic only: the revision carries decisions
# 8-10's unqualified Windows product changes, so nothing here is qualification.
param([string]$rev)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk"; $d = "$root\d12"
if (Test-Path $d) { Write-Host "refusing: $d exists"; exit 1 }
git clone -q --no-checkout "$env:USERPROFILE\d12.bundle" $d; git -C $d checkout -q --detach $rev
Write-Host ("revision " + (git -C $d rev-parse HEAD)); git -C $d status --porcelain --untracked-files=no
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10
Write-Host "build exit $LASTEXITCODE"
$p = "$d\docs\m6\windows-1080p60-wake-d12\probe"
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $p && cl /nologo /O2 /EHsc /std:c++17 stallprobe.cpp /link /out:stallprobe.exe 2>&1" | Select-String -Pattern 'error|warning' | Select-Object -First 5
foreach ($f in "$d\build\fl_gst_local_native_bench_win.exe", "$d\build\gstframelink.dll", "$d\build\framelink.dll", "$d\build\framelink_gst_session.dll", "$p\stallprobe.exe") { if (Test-Path $f) { Write-Host ("sha256 {0} {1}" -f (Split-Path $f -Leaf), (Get-FileHash $f).Hash) } else { Write-Host "missing $f" } }
Set-Location $d; $env:PATH = "$d\build;$env:PATH"; $py = (Get-Command python).Source
& $py tools\test_local_native_bench.py 2>&1 | Select-Object -Last 2 | ForEach-Object { Write-Host "unit: $_" }
$out = "$root\d12-smoke"; if (Test-Path $out) { Remove-Item -Recurse $out }
$env:FL_BENCH_PACE_DIAG = '1'
& $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out $out --cells nv12-1080p60 --duration 10 2>&1 | ForEach-Object { Write-Host "smoke: $_" }
Get-Content "$out\nv12-1080p60-10s\source.pace.json" | ForEach-Object { Write-Host "pace.json: $_" }
& $py tools\attribute_native_stages.py "$out\nv12-1080p60-10s" 2>&1 | ForEach-Object { Write-Host "stages: $_" }
Write-Host done
} finally { $m.ReleaseMutex() }
