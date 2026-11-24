# Formal Windows 4K30 rate cells at 357d82a5 with the display lit throughout, run in the interactive session
# (m6j-task.ps1) so the relay's display hold is honoured. Display idle timeout
# at its normal 300 s. Logs powercfg /requests every 5 min and the Kernel-Power
# 566 display events afterwards.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $log = "$root\m6j-task.log"
if (Test-Path "$root\out-357d82a-4k30-lit") { Write-Host 'refusing: output exists'; exit 1 }
if ((git -C $d rev-parse HEAD) -notlike '357d82a5*') { Write-Host 'wrong revision'; exit 1 }
git -C $d status --porcelain --untracked-files=no
powercfg /q SCHEME_CURRENT SUB_VIDEO VIDEOIDLE | Select-String 'Current AC' | ForEach-Object { Write-Host "timeout: $_" }
Get-Process explorer | ForEach-Object { Write-Host "desktop session $($_.SessionId)" }
if (Test-Path $log) { Remove-Item $log }
$since = Get-Date
schtasks /create /f /tn FLFormal4kLit /sc once /st 23:59 /it /tr "powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $root\m6j-task.ps1" | Out-Null
schtasks /run /tn FLFormal4kLit | Out-Null
$deadline = (Get-Date).AddMinutes(100)
while ((Get-Date) -lt $deadline) {
  Start-Sleep 300
  $r = powercfg /requests; $i = [array]::IndexOf($r, 'DISPLAY:'); $j = [array]::IndexOf($r, 'SYSTEM:')
  Write-Host ("{0} display requests: {1}" -f (Get-Date).ToString('HH:mm:ss'), (($r[($i+1)..($j-1)] | Where-Object { $_ }) -join ' | '))
  if ((Test-Path $log) -and (Select-String -Quiet -Path $log -Pattern ' done$')) { break }
}
if (Test-Path $log) { Get-Content $log | ForEach-Object { Write-Host "task: $_" } } else { Write-Host 'no task log' }
Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-Kernel-Power'; Id=566; StartTime=$since} -ErrorAction SilentlyContinue | Sort-Object TimeCreated | ForEach-Object { $x=[xml]$_.ToXml(); $r=($x.Event.EventData.Data | Where-Object Name -eq 'Reason').'#text'; Write-Host "566 $($_.TimeCreated.ToString('HH:mm:ss.fff')) reason $r" }
schtasks /delete /f /tn FLFormal4kLit | Out-Null
Write-Host finished
} finally { $m.ReleaseMutex() }
