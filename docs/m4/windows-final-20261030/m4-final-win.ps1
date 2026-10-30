$ErrorActionPreference='Stop'
$f='C:/Users/jonsu/dev/framelink-m4-20261030'; $v='C:/Users/jonsu/dev/vkrender-m4-20261030'
tar -xf C:/Users/jonsu/temp/fl-m4-final-win.tar -C $f
tar -xf C:/Users/jonsu/temp/vkr-m4-final-win.tar -C $v
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64='C:/Program Files/gstreamer/1.0/msvc_x86_64'
$env:PATH="$env:GSTREAMER_1_0_ROOT_MSVC_X86_64/bin;$v/build/Release;$env:PATH"
cmake -S $f -B "$f/build-final" -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GSTREAMER=ON
if($LASTEXITCODE){exit $LASTEXITCODE}
cmake --build "$f/build-final" --config Release --parallel 6
if($LASTEXITCODE){exit $LASTEXITCODE}
ctest --test-dir "$f/build-final" -C Release --output-on-failure
exit $LASTEXITCODE
