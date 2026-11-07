$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-20261106'
if (@(Get-Process fl_gst_av_bench -ErrorAction SilentlyContinue).Count -ne 0) { throw 'Existing benchmark process' }
Get-FileHash build/Release/fl_gst_av_bench.exe,build/Release/gstframelink.dll,build/Release/framelink.dll | Format-List