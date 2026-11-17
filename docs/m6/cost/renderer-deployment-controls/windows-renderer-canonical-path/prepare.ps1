$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-20261106'
if (@(Get-Process vkrenderer,framelink_av_producer -ErrorAction SilentlyContinue).Count -ne 0) { throw 'Existing renderer/producer' }
python --version
if ($LASTEXITCODE -ne 0) { throw 'Python unavailable' }
cmake --build build --config Release --target framelink_av_producer -j 4
if ($LASTEXITCODE -ne 0) { throw 'Producer build failed' }
if (Test-Path '<win-home>/dev/m6-render-stage-1794045256') { throw 'Stage exists' }
New-Item -ItemType Directory -Path '<win-home>/dev/m6-render-stage-1794045256' | Out-Null
Copy-Item '<win-home>/dev/vkrender-m5-final-validation/build-final/Release/*' '<win-home>/dev/m6-render-stage-1794045256' -Recurse
Copy-Item '<win-home>/dev/framelink-m6-20261106/build/Release/framelink.dll' '<win-home>/dev/m6-render-stage-1794045256/framelink.dll' -Force
Get-FileHash '<win-home>/dev/m6-render-stage-1794045256/vkrenderer.exe','<win-home>/dev/m6-render-stage-1794045256/framelink.dll','<win-home>/dev/framelink-m6-20261106/build/Release/gstframelink.dll','<win-home>/dev/framelink-m6-20261106/build/Release/framelink_av_producer.exe' | Format-List