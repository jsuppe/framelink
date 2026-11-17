$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-20261106'
python '<win-home>/dev/m6-render-stage-1794045310/probe.py' --build '<win-home>/dev/framelink-m6-20261106/build/Release' --renderer '<win-home>/dev/m6-render-stage-1794045310/vkrenderer.exe' --output '<win-home>/dev/m6-render-stage-1794045310/results' --seconds 5 --case 1920x1080-60-NV12-av --trace on --native
exit $LASTEXITCODE