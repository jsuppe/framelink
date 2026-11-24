# Normal desktop (owner's applications running), NV12 4K30 1800 s: default
# profile, then pool 4 / queue 2. Harness at efa86dec (CSV writes off the
# timed threads). Diagnostic: the desktop load is not controlled.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\out-efa86de-noisy"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
if ((git -C $d rev-parse HEAD) -notlike 'efa86dec*') { Write-Host 'wrong revision'; exit 1 }
New-Item -ItemType Directory $out | Out-Null
$apps = 'LM Studio','NVIDIA Broadcast','steam','EpicGamesLauncher','ollama','ollama app','chrome','Discord','Code','claude'
function state($tag) { $p = Get-Process | Where-Object { $apps -contains $_.ProcessName } | Group-Object ProcessName | ForEach-Object { "$($_.Name)x$($_.Count)" }; Write-Host "$tag $((Get-Date).ToString('HH:mm:ss')) apps: $($p -join ' ')"; (Get-Service ollama -ErrorAction SilentlyContinue).Status | ForEach-Object { Write-Host "$tag ollama service: $_" } }
$env:PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$d\build;$env:PATH"; Set-Location $d
$py = (Get-Command python).Source
foreach ($p in @(@('p2q1',2,1),@('p4q2',4,2))) {
 state "before-$($p[0])"
 & $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out "$out\$($p[0])" --cells nv12-4k30 --duration 1800 --pool $p[1] --video-queue $p[2] 2>&1 | ForEach-Object { Write-Host "$($p[0]) $((Get-Date).ToString('HH:mm:ss')): $_" }
}
state 'after'
Write-Host done
} finally { $m.ReleaseMutex() }
