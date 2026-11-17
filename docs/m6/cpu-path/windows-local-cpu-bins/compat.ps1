$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-windows-3f45f7b'
ctest --test-dir build -C Release --output-on-failure -R '^gst_av_bin$'
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1
& $dump.FullName /exports build/Release/framelink_gst_session.dll
exit $LASTEXITCODE
