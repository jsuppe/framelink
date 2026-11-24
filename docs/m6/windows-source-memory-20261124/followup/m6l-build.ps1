# Build 648fe9de (diagnostic --upload staging, runs up to 3600 s) and smoke the
# staging upload: a 20 s NV12 4K30 capture with --upload staging must pass the
# analyzer and record "upload":"staging".
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\out-648fe9d-smoke"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
git -C $d fetch -q "$env:USERPROFILE\m6h.bundle" "feature/m6-lifecycle:refs/remotes/bundle/m6-lifecycle"
git -C $d reset -q --hard; git -C $d clean -qfdx; git -C $d checkout -q --detach 648fe9de
Write-Host ("revision " + (git -C $d rev-parse HEAD)); git -C $d status --porcelain --untracked-files=no
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
$py = (Get-Command python).Source
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10
Write-Host "build exit $LASTEXITCODE"
Set-Location $d; $env:PATH = "$d\build;$env:PATH"
Write-Host ("bench sha256 " + (Get-FileHash "$d\build\fl_gst_local_native_bench_win.exe").Hash)
Write-Host ("plugin sha256 " + (Get-FileHash "$d\build\gstframelink.dll").Hash)
& $py tools\test_local_native_bench.py 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Host "unit: $_" }
New-Item -ItemType Directory $out | Out-Null
$p = Start-Process -PassThru -NoNewWindow "$d\build\fl_gst_local_native_bench_win.exe" -ArgumentList "`"$d\build\gstframelink.dll`" --out `"$out\nv12-4k30-20s`" --seconds 20 --width 3840 --height 2160 --fps 30 --format NV12 --upload staging"
$p.WaitForExit(); Write-Host "bench exit $($p.ExitCode)"
Get-Content "$out\nv12-4k30-20s\config.json" | ForEach-Object { Write-Host "config: $_" }
& $py -c "import sys;sys.path.insert(0,'tools');from check_local_native_bench import analyze;r=analyze(r'$out\nv12-4k30-20s');t=r['timing_ms']['scheduled_to_final_arrival'];u=r['timing_ms'].get('source_upload_call');print('analysis pass_all',r['pass_all'],'arrival p99/max',round(t['p99'],2),round(t['max'],2),'upload p99/max',u and (round(u['p99'],2),round(u['max'],2)))" 2>&1 | ForEach-Object { Write-Host "$_" }
} finally { $m.ReleaseMutex() }
