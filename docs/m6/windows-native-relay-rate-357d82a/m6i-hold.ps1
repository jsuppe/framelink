# Build 357d82a5 (relay holds the display awake), restore the display idle
# timeout to 5 min, then check the hold: with the timeout at 1 min for the
# check only, a 150 s NV12 4K30 capture must log no idle display-off (566
# reason 12), and powercfg /requests must list the bench while it runs.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\out-357d82a-hold"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
git -C $d fetch -q "$env:USERPROFILE\m6h.bundle" "feature/m6-lifecycle:refs/remotes/bundle/m6-lifecycle"
git -C $d reset -q --hard; git -C $d clean -qfdx; git -C $d checkout -q --detach 357d82a5
Write-Host ("revision " + (git -C $d rev-parse HEAD)); git -C $d status --porcelain --untracked-files=no
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
$py = (Get-Command python).Source
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10
Write-Host "build exit $LASTEXITCODE"
Set-Location $d; $env:PATH = "$d\build;$env:PATH"
Write-Host ("bench sha256 " + (Get-FileHash "$d\build\fl_gst_local_native_bench_win.exe").Hash)
& $py tools\test_local_native_bench.py 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Host "unit: $_" }
New-Item -ItemType Directory $out | Out-Null
powercfg /change monitor-timeout-ac 1
$since = Get-Date
$p = Start-Process -PassThru -NoNewWindow "$d\build\fl_gst_local_native_bench_win.exe" -ArgumentList "`"$d\build\gstframelink.dll`" --out `"$out\nv12-4k30-150s`" --seconds 150 --width 3840 --height 2160 --fps 30 --format NV12"
Start-Sleep 20
powercfg /requests 2>&1 | ForEach-Object { Write-Host "requests: $_" }
$p.WaitForExit(); Write-Host "bench exit $($p.ExitCode)"
Get-Content "$out\nv12-4k30-150s\config.json" | ForEach-Object { Write-Host "config: $_" }
& $py -c "import sys;sys.path.insert(0,'tools');from check_local_native_bench import analyze;r=analyze(r'$out\nv12-4k30-150s');t=r['timing_ms']['scheduled_to_final_arrival'];print('analysis pass_all',r['pass_all'],'arrival max',round(t['max'],2))" 2>&1 | ForEach-Object { Write-Host "$_" }
Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-Kernel-Power'; Id=566; StartTime=$since} -ErrorAction SilentlyContinue | Sort-Object TimeCreated | ForEach-Object { $x=[xml]$_.ToXml(); $r=($x.Event.EventData.Data | Where-Object Name -eq 'Reason').'#text'; Write-Host "566 $($_.TimeCreated.ToString('HH:mm:ss.fff')) reason $r" }
Write-Host "checked $($since.ToString('HH:mm:ss'))-$((Get-Date).ToString('HH:mm:ss')); waiting 90 s with no bench"
$idle = Get-Date; Start-Sleep 90
Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-Kernel-Power'; Id=566; StartTime=$idle} -ErrorAction SilentlyContinue | Sort-Object TimeCreated | ForEach-Object { $x=[xml]$_.ToXml(); $r=($x.Event.EventData.Data | Where-Object Name -eq 'Reason').'#text'; Write-Host "control 566 $($_.TimeCreated.ToString('HH:mm:ss.fff')) reason $r" }
} finally { powercfg /change monitor-timeout-ac 5; powercfg /q SCHEME_CURRENT SUB_VIDEO VIDEOIDLE | Select-String 'Current AC' | ForEach-Object { Write-Host "restored: $_" }; $m.ReleaseMutex() }
