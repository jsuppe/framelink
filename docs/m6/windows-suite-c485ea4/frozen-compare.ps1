# Is the intermittent read_lease_d3d11_producer_death pixel failure in the frozen revision too? 400 repeats each.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(30)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '%USERPROFILE%\dev\apichk'
foreach ($n in 'old','new','suite-c485ea4') { $b = "$root\$n\build"
  Write-Host ("$n rev " + (git -C "$root\$n" rev-parse --short HEAD) + " framelink.dll " + (Get-FileHash "$b\framelink.dll").Hash.Substring(0,16) + " test " + (Get-FileHash "$b\fl_read_lease_d3d11.exe").Hash.Substring(0,16)) }
foreach ($n in 'old','suite-c485ea4') { $b = "$root\$n\build"; $env:PATH = "$b;" + $env:PATH
  $pass = 0; $fail = @()
  for ($i = 1; $i -le 400; $i++) {
    $p = Start-Process -PassThru -NoNewWindow -Wait -FilePath "$b\fl_read_lease_d3d11.exe" -ArgumentList '--producer-death' -RedirectStandardError "$root\fz-err.txt" -RedirectStandardOutput "$root\fz-out.txt"
    if ($p.ExitCode -eq 0) { $pass++ } else { $fail += "${i}:" + (Get-Content "$root\fz-err.txt" -Raw).Trim() }
  }
  Write-Host "$n producer-death: pass $pass/400; failures: $($fail -join ' | ')" }
} finally { $m.ReleaseMutex() }
