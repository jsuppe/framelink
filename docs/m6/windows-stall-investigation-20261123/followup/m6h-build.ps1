$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\out-efa86de"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
git -C $d fetch -q "$env:USERPROFILE\m6h.bundle" "feature/m6-lifecycle:refs/remotes/bundle/m6-lifecycle"
git -C $d reset -q --hard; git -C $d clean -qfdx; git -C $d checkout -q --detach efa86dec
Write-Host ("revision " + (git -C $d rev-parse HEAD)); git -C $d status --porcelain --untracked-files=no
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
$py = (Get-Command python).Source
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10
Write-Host "build exit $LASTEXITCODE"
Set-Location $d; $env:PATH = "$d\build;$env:PATH"
Write-Host ("bench sha256 " + (Get-FileHash "$d\build\fl_gst_local_native_bench_win.exe").Hash)
& $py tools\test_local_native_bench.py 2>&1 | Select-Object -Last 2 | ForEach-Object { Write-Host "unit: $_" }
New-Item -ItemType Directory $out | Out-Null
& $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out "$out\p2q1" --cells nv12-4k30 bgra1080p60 --duration 30 2>&1 | ForEach-Object { Write-Host "p2q1: $_" }
& $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out "$out\p4q2" --cells nv12-4k30 bgra1080p60 --duration 30 --pool 4 --video-queue 2 2>&1 | ForEach-Object { Write-Host "p4q2: $_" }
Write-Host done
} finally { $m.ReleaseMutex() }
