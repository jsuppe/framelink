$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-20261106'
cmake --build build --config Release --target fl_native_boundary_benchmark_win -j 4
exit $LASTEXITCODE