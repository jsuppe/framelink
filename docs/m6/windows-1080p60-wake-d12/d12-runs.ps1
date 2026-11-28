# Decision 12 A2: the Windows native 1080p60 late wake-up, diagnosed.
# Launched over ssh like the 060c54e4 batch (non-interactive; the display is
# left to its normal idle timeout). Holds the validation mutex for the batch.
#  - FL_BENCH_PACE_DIAG=1 on every capture (source.pace.csv);
#  - a rolling in-memory WPR trace (CPU sampling, context switches and ready
#    threads, disk and file I/O) for the whole batch. A watcher saves it as
#    soon as any bench process reports a failed CHECK, then restarts it;
#  - the FrameLink-free probe at 60 Hz with a 3 MB fill per tick, alongside,
#    on the same QPC clock, so a late wake-up in the bench can be matched
#    against a thread that touches no FrameLink, GStreamer or GPU;
#  - optional labelled experiments (-experiment): highres, mmcss, nothrottle.
param([string]$name, [int]$reps = 3, [int]$duration = 1800, [string[]]$cells = @('nv12-1080p60', 'bgra1080p60'), [string]$experiment = 'none', [int]$maxSaves = 4)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(8)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk"; $d = "$root\d12"; $out = "$root\$name"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
New-Item -ItemType Directory $out | Out-Null
$log = "$out\run.log"
function L($s) { $line = "{0} {1}" -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ'), $s; Add-Content $log $line; Write-Host $line }
L ("revision " + (git -C $d rev-parse HEAD) + " experiment $experiment reps $reps duration $duration cells " + ($cells -join ','))
foreach ($f in "$d\build\fl_gst_local_native_bench_win.exe", "$d\build\gstframelink.dll", "$d\build\framelink.dll", "$d\build\framelink_gst_session.dll") { L ("sha256 {0} {1}" -f (Split-Path $f -Leaf), (Get-FileHash $f).Hash) }
Get-Process | Select-Object Name, Id, SessionId, StartTime | ConvertTo-Json | Set-Content "$out\processes-start.json"
powercfg /getactivescheme | Set-Content "$out\power.txt"; powercfg /requests | Set-Content "$out\requests-start.txt"
nvidia-smi --query-gpu=name,driver_version,pstate,clocks.gr,clocks.mem --format=csv | Set-Content "$out\gpu-start.csv"
Get-CimInstance Win32_ShadowCopy | Select-Object ID, InstallDate | ConvertTo-Json | Set-Content "$out\shadow-copies-start.json"

$wprArgs = '-start CPU -start DiskIO -start FileIO'
& wpr.exe -cancel 2>$null | Out-Null
& cmd /c "wpr.exe $wprArgs" | Out-Null; L "wpr start exit $LASTEXITCODE"
$total = $reps * $cells.Count * ($duration + 120) + 600
$probe = Start-Process -FilePath "$d\docs\m6\windows-1080p60-wake-d12\probe\stallprobe.exe" -ArgumentList @("$out\probe", "$total", '--video-hz', '60', '--fill-mb', '3', '--log-ms', '10') -RedirectStandardOutput "$out\probe.log" -RedirectStandardError "$out\probe.err" -PassThru -WindowStyle Hidden
L "probe pid $($probe.Id) for $total s"

$env:PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$d\build;$env:PATH"; Set-Location $d
$py = (Get-Command python).Source
$env:FL_BENCH_PACE_DIAG = '1'
switch ($experiment) {
  'none' {}
  'highres' { $env:FL_BENCH_PACE_TIMER = 'highres' }
  'mmcss' { $env:FL_BENCH_MMCSS = '1' }
  'nothrottle' { $env:FL_BENCH_NO_THROTTLE = '1' }
  default { L "unknown experiment $experiment"; exit 2 }
}
$saves = 0
foreach ($rep in 1..$reps) {
  foreach ($cell in $cells) {
    $cap = "$out\$cell-$rep"
    $etl = "$out\$cell-$rep.etl"
    # The watcher saves the rolling trace at the first failed CHECK in any role's stderr.
    # It logs to $etl.watch.log itself: a save (wpr -stop) takes tens of seconds,
    # and the batch waits for it before the next capture.
    $wlog = "$etl.watch.log"
    $watch = Start-Job -ArgumentList $cap, $etl, $wprArgs, ($saves -lt $maxSaves), $wlog -ScriptBlock {
      param($cap, $etl, $wprArgs, $allowed, $wlog)
      while ($true) {
        $hit = Get-ChildItem -Path $cap -Recurse -Filter '*.stderr.log' -ErrorAction SilentlyContinue | Where-Object { $_.Length -gt 0 } | Select-String -Pattern 'fl_gst_local_native_bench\.cpp:\d+:' -List | Select-Object -First 1
        if ($hit) {
          Add-Content $wlog ("{0} hit {1}:{2} {3}" -f (Get-Date).ToUniversalTime().ToString('o'), $hit.Path, $hit.LineNumber, $hit.Line)
          if ($allowed) {
            Start-Sleep -Milliseconds 1500
            & cmd /c "wpr.exe -stop `"$etl`"" | Out-Null; $stop = $LASTEXITCODE
            Add-Content $wlog ("{0} saved {1} stop={2} size={3}" -f (Get-Date).ToUniversalTime().ToString('o'), $etl, $stop, $(if (Test-Path $etl) { (Get-Item $etl).Length } else { 0 }))
            & cmd /c "wpr.exe $wprArgs" | Out-Null
            Add-Content $wlog ("{0} restarted wpr exit={1}" -f (Get-Date).ToUniversalTime().ToString('o'), $LASTEXITCODE)
          } else { Add-Content $wlog 'save limit reached' }
          return
        }
        Start-Sleep -Milliseconds 250
      }
    }
    L "START $cell rep $rep"
    & $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out $cap --cells $cell --duration $duration 2>&1 | ForEach-Object { L "  $cell-$rep : $_" }
    L "END $cell rep $rep exit $LASTEXITCODE"
    Wait-Job $watch -Timeout 5 | Out-Null
    if ((Test-Path $wlog) -and $watch.State -eq 'Running') { L '  watcher: saving the trace'; Wait-Job $watch -Timeout 900 | Out-Null }
    Stop-Job $watch -ErrorAction SilentlyContinue; Remove-Job $watch -Force
    if (Test-Path $wlog) { Get-Content $wlog | ForEach-Object { L "  watcher: $_" }; if (Select-String -Path $wlog -Pattern ' saved ' -Quiet) { $saves++ } }
  }
}
& wpr.exe -cancel 2>$null | Out-Null; L 'wpr cancelled'
if (-not $probe.HasExited) { Stop-Process -Id $probe.Id; L 'probe stopped' }
powercfg /requests | Set-Content "$out\requests-end.txt"
Get-CimInstance Win32_ShadowCopy | Select-Object ID, InstallDate | ConvertTo-Json | Set-Content "$out\shadow-copies-end.json"
Get-Process | Select-Object Name, Id, SessionId, StartTime | ConvertTo-Json | Set-Content "$out\processes-end.json"
$from = (Get-Item $out).CreationTime
Get-WinEvent -FilterHashtable @{LogName = 'System'; StartTime = $from } -ErrorAction SilentlyContinue | Select-Object @{n = 'utc'; e = { $_.TimeCreated.ToUniversalTime().ToString('o') } }, ProviderName, Id, LevelDisplayName, @{n = 'msg'; e = { ($_.Message -split "`n")[0] } } | ConvertTo-Json | Set-Content "$out\system-events.json"
Get-WinEvent -FilterHashtable @{LogName = 'Microsoft-Windows-Windows Defender/Operational'; StartTime = $from } -ErrorAction SilentlyContinue | Select-Object @{n = 'utc'; e = { $_.TimeCreated.ToUniversalTime().ToString('o') } }, Id, @{n = 'msg'; e = { ($_.Message -split "`n")[0] } } | ConvertTo-Json | Set-Content "$out\defender-events.json"
L 'done'
} finally { $m.ReleaseMutex() }
