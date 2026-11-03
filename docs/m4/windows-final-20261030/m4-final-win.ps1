$ErrorActionPreference='Stop'
$f='<win-home>/dev/framelink-m4-20261030'; $v='<win-home>/dev/vkrender-m4-20261030'
tar -xf <win-home>/temp/fl-m4-final-win.tar -C $f
tar -xf <win-home>/temp/vkr-m4-final-win.tar -C $v
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64='C:/Program Files/gstreamer/1.0/msvc_x86_64'
$env:PATH="$env:GSTREAMER_1_0_ROOT_MSVC_X86_64/bin;$v/build/Release;$env:PATH"
cmake -S $f -B "$f/build-final" -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GSTREAMER=ON
if($LASTEXITCODE){exit $LASTEXITCODE}
cmake --build "$f/build-final" --config Release --parallel 6
if($LASTEXITCODE){exit $LASTEXITCODE}
ctest --test-dir "$f/build-final" -C Release --output-on-failure
exit $LASTEXITCODE
