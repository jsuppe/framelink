$ErrorActionPreference='Stop'
Set-Location '<win-home>/dev/m6-renderer9f74b9d-framelinka50841a-20261112'
$m=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation');if(!$m.WaitOne(0)){throw 'runtime held'}
try {
 if(@(Get-Process | Where-Object {$_.ProcessName -match '^fl_|^framelink|^vkrenderer'}).Count){throw 'runtime busy'}
 if((Get-FileHash framelink/test/fl_gst_av_bench.cpp).Hash.ToLowerInvariant() -ne '55573459f141299ee44ad23734bfa273b57f9635114862b16df0a52c75ddf7d8'){throw 'harness mismatch'}
 cmake -S renderer -B build -A x64 -DFRAMELINK_DIR='<win-home>/dev/m6-renderer9f74b9d-framelinka50841a-20261112/framelink' -DFRAMELINK_BUILD_GST=ON -DFRAMELINK_BUILD_SAMPLES=ON -DFRAMELINK_BUILD_TESTS=ON
 if($LASTEXITCODE -ne 0){throw 'configure failed'}
 cmake --build build --config Release --target fl_gst_av_bench -- /nr:false /m:2
 if($LASTEXITCODE -ne 0){throw 'build failed'}
 Get-FileHash build/framelink/Release/fl_gst_av_bench.exe,build/framelink/Release/gstframelink.dll,build/framelink/Release/framelink.dll,build/framelink/Release/framelink_gst_session.dll | Select-Object Path,Hash | ConvertTo-Json | Set-Content results/lifecycle-build-pins.json
} finally {$m.ReleaseMutex();$m.Dispose()}
