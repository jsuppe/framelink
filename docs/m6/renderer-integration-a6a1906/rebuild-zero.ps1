$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$m=[System.Threading.Mutex]::new($false,'Local\FrameLinkFocusedWindowsValidation')
if(-not $m.WaitOne(0)){throw 'lane held'}
try {
 if(@(Get-Process vkrenderer,framelink_av_producer,ctest,cmake,MSBuild -ErrorAction SilentlyContinue).Count){throw 'lane busy'}
 $env:MSBUILDDISABLENODEREUSE='1'
 & cmake --build build --config Release --target vkrenderer -- /nr:false /m:2 *> results/zero-rebuild.log
 $rc=$LASTEXITCODE
 Get-FileHash renderer/renderer/main.cpp,renderer/docs/M4_PLAYBACK.md,build/Release/vkrenderer.exe | ConvertTo-Json | Set-Content results/zero-build-pins.json
 @{exit=$rc;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content results/zero-rebuild-status.json
} finally {$m.ReleaseMutex();$m.Dispose()}
exit $rc
