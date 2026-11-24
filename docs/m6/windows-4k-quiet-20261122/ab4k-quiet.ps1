$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$out = '<win-home>\dev\apichk\out-ab4k-quiet'
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
New-Item -ItemType Directory $out | Out-Null
$env:PATH = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;' + $env:PATH
Remove-Item Env:GST_PLUGIN_PATH -ErrorAction SilentlyContinue
$old = '<win-home>\dev\framelink-final-final-060c54e4\build\Release'
$new = '<win-home>\dev\apichk\rate0e8\build'
$pins = @{ 'fl_gst_local_native_bench_win.exe'='4F5FF14B249277C0B129B4E88E9EC78B9E258086B4BBC7BDE7F3528A04A1903A'; 'framelink.dll'='14B84733E8AB484F7D71595494A7124BFD3643622C49DFD91315DBCE54DE7403'; 'framelink_gst_session.dll'='9F3AB33DA330AFF91376700E0ECC9F861CEDD4F340A15105F2C29CA11298B41D'; 'gstframelink.dll'='4AC101887796478EF46AB2C09F8AFE11BF754007130321058CE3FD3C32735F4E' }
foreach ($k in $pins.Keys) { $h = (Get-FileHash "$old\$k").Hash; if ($h -ne $pins[$k]) { Write-Host "pin mismatch $k $h"; exit 2 } }
Write-Host 'frozen pins match'
$pat0 = '^(ollama|LM Studio|NVIDIA Broadcast|steam|EpicGamesLauncher)$'; if (Get-Process | Where-Object { $_.Name -match $pat0 }) { Write-Host 'host not quiet'; exit 4 }; if ((Get-Service OllamaServe).Status -ne 'Stopped') { Write-Host 'ollama service running'; exit 4 }; Write-Host 'host quiet'
Write-Host ('new exe ' + (Get-FileHash "$new\fl_gst_local_native_bench_win.exe").Hash)
foreach ($arm in @(@('frozen-060c54e4',$old), @('current-0e8da98',$new))) {
  $label = $arm[0]; $b = $arm[1]; $dir = "$out\$label"
  $saved = $env:PATH; $env:PATH = "$b;$env:PATH"
  $t0 = Get-Date
  & "$b\fl_gst_local_native_bench_win.exe" "$b\gstframelink.dll" --out $dir --seconds 1800 --width 3840 --height 2160 --fps 30 --format NV12 *> "$out\$label.log"
  Write-Host ("arm $label exit $LASTEXITCODE start " + $t0.ToString('o') + ' end ' + (Get-Date).ToString('o'))
  $env:PATH = $saved
}
$pat = '^(ollama|LM Studio|NVIDIA Broadcast|steam|steamwebhelper|EpicGamesLauncher|EpicWebHelper|EOSOverlayRenderer-Win64-Shipping)$'
$back = Get-Process | Where-Object { $_.Name -match $pat }; if ($back) { Write-Host ('closed apps came back: ' + (($back | ForEach-Object { $_.Name }) -join ',')) } else { Write-Host 'closed apps still closed' }
$d = '<win-home>\dev\apichk\rate0e8'; Set-Location $d; $env:PATH = "$d\build;$env:PATH"; $py = (Get-Command python).Source
Write-Host ("revision " + (git -C $d rev-parse HEAD)); git -C $d status --porcelain --untracked-files=no
& $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out "$out\cells-0e8da98-quiet" --cells bgra4k30 nv12-4k30 --duration 1800 2>&1 | ForEach-Object { Write-Host "cells: $_" }
$back = Get-Process | Where-Object { $_.Name -match $pat }; if ($back) { Write-Host ('closed apps came back: ' + (($back | ForEach-Object { $_.Name }) -join ',')) } else { Write-Host 'closed apps still closed' }
Write-Host done
} finally { $m.ReleaseMutex() }
