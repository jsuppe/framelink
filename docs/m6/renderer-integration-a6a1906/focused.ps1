$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$m=[System.Threading.Mutex]::new($false,'Local\FrameLinkFocusedWindowsValidation')
if(-not $m.WaitOne(0)){throw 'lane held'}
try {
 $p=@(Get-Process | Where-Object {$_.ProcessName -match '^fl_|^framelink|^vkrenderer|^ctest|^cmake|^MSBuild'})
 if($p.Count){throw 'lane busy'}
 $env:PATH="$PSScriptRoot/build/Release;$PSScriptRoot/build/framelink/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;"+$env:PATH
 $pins=Get-Content results/binary-sha256.json -Raw | ConvertFrom-Json
 foreach($e in $pins.PSObject.Properties){if((Get-FileHash (Join-Path build $e.Name)).Hash.ToLower() -ne $e.Value){throw 'binary changed'}}
 & ctest --test-dir build -C Release --output-on-failure --timeout 60 --no-tests=error -R '^(av_schedule|av_clock_contract|headless_pacer|nv12_cpu)$' --output-junit "$PSScriptRoot/results/focused.xml" *> results/focused.log
 $rc=$LASTEXITCODE
 @{exit=$rc;ended_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content results/focused-status.json
} finally {$m.ReleaseMutex();$m.Dispose()}
exit $rc
