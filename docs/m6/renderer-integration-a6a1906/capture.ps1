param([int]$Latency=100,[string]$Label='legacy100')
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$m=[System.Threading.Mutex]::new($false,'Local\FrameLinkFocusedWindowsValidation')
if(-not $m.WaitOne(0)){throw 'lane held'}
try {
 if(@(Get-Process vkrenderer,framelink_av_producer,ctest,cmake,MSBuild -ErrorAction SilentlyContinue).Count){throw 'lane busy'}
 $env:PATH="$PSScriptRoot/build/Release;$PSScriptRoot/build/framelink/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;"+$env:PATH
 & python -E probe.py --build "$PSScriptRoot/build/framelink/Release" --renderer "$PSScriptRoot/build/Release/vkrenderer.exe" --output "$PSScriptRoot/results/$Label" --seconds 5 --case 1920x1080-60-NV12-av --trace on --native --latency-ms $Latency *> "results/$Label-controller.log"
 $rc=$LASTEXITCODE
 $p=@(Get-Process vkrenderer,framelink_av_producer -ErrorAction SilentlyContinue)
 @{exit=$rc;idle_count=$p.Count;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content "results/$Label-status.json"
} finally {$m.ReleaseMutex();$m.Dispose()}
exit $rc
