$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
 $out = '<win-home>\dev\apichk\stallprobe\noisy'
 if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
 New-Item -ItemType Directory $out | Out-Null
 $pat = '^(ollama|LM Studio|NVIDIA Broadcast|steam|EpicGamesLauncher)$'
 $up = @(Get-Process | Where-Object { $_.Name -match $pat } | ForEach-Object Name | Sort-Object -Unique); Write-Host ("apps running: " + ($up -join ','))
 Get-Process | Select-Object Name,Id,Path,SessionId | ConvertTo-Json | Set-Content "$out\processes.json"
 nvidia-smi --query-compute-apps=pid,process_name --format=csv | Set-Content "$out\gpu-clients.csv"
 $env:PATH = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;' + $env:PATH; Remove-Item Env:GST_PLUGIN_PATH -ErrorAction SilentlyContinue
 $b = '<win-home>\dev\apichk\rate0e8\build'; $env:PATH = "$b;$env:PATH"
 Write-Host ('exe ' + (Get-FileHash "$b\fl_gst_local_native_bench_win.exe").Hash)
 function Pair($tag) { "{0},{1},{2}" -f $tag,[System.Diagnostics.Stopwatch]::GetTimestamp(),(Get-Date).ToString('o') | Add-Content "$out\qpc-wall.csv" }
 "tag,qpc_ticks,wall" | Set-Content "$out\qpc-wall.csv"; Pair 'begin'
 Write-Host ("qpc freq " + [System.Diagnostics.Stopwatch]::Frequency)
 wpr.exe -cancel 2>$null | Out-Null
 wpr.exe -start CPU -start DiskIO -start FileIO -filemode; Write-Host "wpr start $LASTEXITCODE"
 $smi = Start-Process nvidia-smi -ArgumentList '--query-gpu=timestamp,pstate,clocks.gr,clocks.mem,utilization.gpu,power.draw','--format=csv','-lms','250','-f',"$out\gpu-state.csv" -NoNewWindow -PassThru
 $probe = Start-Process "<win-home>\dev\apichk\stallprobe\stallprobe.exe" -ArgumentList @("$out\probe",'1830','--log-ms','5') -RedirectStandardOutput "$out\probe.log" -NoNewWindow -PassThru
 $t0 = Get-Date; $i = 0
 while (((Get-Date) - $t0).TotalSeconds -lt 1780) {
  $left = [int](1800 - ((Get-Date) - $t0).TotalSeconds); if ($left -lt 20) { break }
  $i++; $dir = "$out\bench-$i"; $ts = Get-Date; Pair "bench-$i-start"
  & "$b\fl_gst_local_native_bench_win.exe" "$b\gstframelink.dll" --out $dir --seconds $left --width 3840 --height 2160 --fps 30 --format NV12 *> "$out\bench-$i.log"
  $rc = $LASTEXITCODE; Pair "bench-$i-end"
  Write-Host ("bench-$i seconds $left exit $rc start " + $ts.ToString('o') + ' end ' + (Get-Date).ToString('o'))
 }
 $probe.WaitForExit()
 Pair 'end'
 wpr.exe -stop "$out\run.etl" 'noisy host 4K30 + probe'; Write-Host "wpr stop $LASTEXITCODE"
 Stop-Process -Id $smi.Id -ErrorAction SilentlyContinue
 Get-Content "$out\probe.log" | Select-Object -Last 2
 Get-Content "$out\probe\events.csv"
 Write-Host ("etl bytes " + (Get-Item "$out\run.etl").Length)
 Write-Host done
} finally { $m.ReleaseMutex() }
