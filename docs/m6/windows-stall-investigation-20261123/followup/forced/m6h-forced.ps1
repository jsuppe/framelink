# Forced display off/on during NV12 4K30 600 s: pool 4 / queue 2, then the
# default 2/1 as a control. Display idle timeout is disabled (AC 0) so only the
# forced changes occur. Harness at efa86dec.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\out-forced-display"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
if ((git -C $d rev-parse HEAD) -notlike 'efa86dec*') { Write-Host 'wrong revision'; exit 1 }
New-Item -ItemType Directory $out | Out-Null
Get-Process explorer | ForEach-Object { Write-Host "desktop session $($_.SessionId)" }
powercfg /q SCHEME_CURRENT SUB_VIDEO VIDEOIDLE | Select-String 'Current AC'
$env:PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$d\build;$env:PATH"; Set-Location $d
$py = (Get-Command python).Source
foreach ($p in @(@('p4q2',4,2),@('p2q1',2,1))) {
 $log = "$out\$($p[0])-display.log"
 Set-Content "$root\blink.cfg" @("$out\$($p[0])\nv12-4k30-600s", $log)
 schtasks /create /f /tn FLBlink /sc once /st 23:59 /it /tr "powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $root\m6h-blink.ps1" | Out-Null
 schtasks /run /tn FLBlink | Out-Null
 $since = Get-Date
 & $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out "$out\$($p[0])" --cells nv12-4k30 --duration 600 --pool $p[1] --video-queue $p[2] 2>&1 | ForEach-Object { Write-Host "$($p[0]) $((Get-Date).ToString('HH:mm:ss')): $_" }
 Start-Sleep 5; schtasks /end /tn FLBlink 2>$null | Out-Null
 if (Test-Path $log) { Get-Content $log | ForEach-Object { Write-Host "$($p[0]) display: $_" } } else { Write-Host "$($p[0]) display: no log" }
 Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-Kernel-Power'; Id=566; StartTime=$since} -ErrorAction SilentlyContinue | Sort-Object TimeCreated | ForEach-Object { $x=[xml]$_.ToXml(); $r=($x.Event.EventData.Data | Where-Object Name -eq 'Reason').'#text'; Write-Host "$($p[0]) 566 $($_.TimeCreated.ToString('HH:mm:ss.fff')) reason $r" }
}
schtasks /delete /f /tn FLBlink | Out-Null
Write-Host done
} finally { $m.ReleaseMutex() }
