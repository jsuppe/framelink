# Decision 12 B1, Windows: the same six captures with trace capacity at its 1,048,576 cap (the
# first set lost trace events to capacity; its failures stand). Reuses the
# d12-vkr build. Under the validation mutex.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(2)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk\d12-vkr"; $out = "$root\results-1m"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
New-Item -ItemType Directory $out | Out-Null
$renderer = Get-ChildItem "$root\vkrender\build" -Recurse -Filter vkrenderer.exe | Select-Object -First 1
$b = (Get-ChildItem "$root\vkrender\build" -Recurse -Filter framelink_av_producer.exe | Select-Object -First 1).DirectoryName
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$b;$($renderer.DirectoryName);C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$env:PATH"
$py = (Get-Command python).Source
foreach ($lat in 0, 100) { foreach ($rep in 1..3) {
  $label = "lat$lat-$rep"
  & $py -E "$env:USERPROFILE\d12-vkr-probe8x.py" --build $b --renderer $renderer.FullName --output "$out\$label" --seconds 5 --case 1920x1080-60-NV12-av --trace on --native --latency-ms $lat *> "$out\$label-controller.log"
  Write-Host "$label exit $LASTEXITCODE"
} }
Write-Host done
} finally { $m.ReleaseMutex() }
