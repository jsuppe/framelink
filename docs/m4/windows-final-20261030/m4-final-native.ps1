$v='C:/Users/jonsu/dev/vkrender-m4-20261030';$f='C:/Users/jonsu/dev/framelink-m4-20261030'
$env:PATH="C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;$v/build/Release;$env:PATH"
$r="$v/build/Release/vkrenderer.exe";$p="$f/build-final/Release/framelink_av_producer.exe";$g="$f/build-final/Release/gstframelink.dll"
ctest --test-dir "$v/build" -C Release -R 'av_schedule|av_clock_contract|control_http|control_preview_admission' --output-on-failure
if($LASTEXITCODE){exit $LASTEXITCODE}
python "$v/test/av_renderer_smoke.py" $r $p $g
if($LASTEXITCODE){exit $LASTEXITCODE}
python "$v/test/av_renderer_smoke.py" $r $p $g --format NV12
if($LASTEXITCODE){exit $LASTEXITCODE}
python "$v/test/av_renderer_pause.py" $r $p $g
if($LASTEXITCODE){exit $LASTEXITCODE}
python "$v/test/av_renderer_select.py" $r $p $g
if($LASTEXITCODE){exit $LASTEXITCODE}
python "$v/test/av_renderer_generations.py" $r $p $g --switches 10
exit $LASTEXITCODE
