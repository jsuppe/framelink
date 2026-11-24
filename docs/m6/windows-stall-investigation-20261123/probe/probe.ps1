param([string]$name,[int]$seconds,[switch]$wpr,[double]$trigger=50,[int]$saves=3)
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
 $d = '<win-home>\dev\apichk\stallprobe'; $out = "$d\$name"
 if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
 New-Item -ItemType Directory $out | Out-Null; Get-Process | Select-Object Name,Id,Path | ConvertTo-Json | Set-Content "$out\processes.json"; nvidia-smi --query-compute-apps=pid,process_name --format=csv | Set-Content "$out\gpu.csv"; powercfg /getactivescheme | Set-Content "$out\power.txt"
 $a = @($out, $seconds)
 if ($wpr) { $a += @('--wpr', '-start CPU -start DiskIO -start FileIO', '--trigger-ms', $trigger, '--max-saves', $saves) }
 & "$d\stallprobe.exe" @a
 Write-Host "exit $LASTEXITCODE"
} finally { $m.ReleaseMutex() }
