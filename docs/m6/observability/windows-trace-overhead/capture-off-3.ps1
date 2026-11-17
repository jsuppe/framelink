$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-20261106'
New-Item -ItemType Directory -Force '<win-home>/dev/framelink-m6-20261106/m6-boundary-1794046185/off-3' | Out-Null
$env:FL_BENCH_FRAMES='300'
$env:FL_BENCH_WARMUP='60'
$env:FL_BENCH_FPS='60'
$env:FL_BENCH_HOPS='1'
$env:FL_BENCH_WIDTH='1920'
$env:FL_BENCH_HEIGHT='1080'
$env:FL_BENCH_AUDIO='1'
$env:FL_BENCH_FORMAT='BGRA'
$env:FL_BENCH_OUT='<win-home>/dev/framelink-m6-20261106/m6-boundary-1794046185/off-3/capture'
$env:FRAMELINK_TRACE=''
$env:FRAMELINK_TRACE_EVENTS='65536'
$env:FL_BENCH_TRACE=''
$env:FL_BENCH_TRACE_EVENTS='65536'
./build/Release/fl_native_boundary_benchmark_win.exe
exit $LASTEXITCODE