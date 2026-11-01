$d='C:/Users/jonsu/dev';$r="$d/vkrender-m5-final-validation/build-final";$b="$r/framelink/Release";$t="$d/framelink-m5-final-validation/test";$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
python "$t/m5_startup.py" --build $b --output "$d/m5-combined-v2-startup" *> "$d/m5-combined-v2-startup.log"
python "$t/m4_app_offsets.py" "$b/framelink_av_producer.exe" "$b/framelink_av_consumer.exe" "$b/gstframelink.dll" --clock-policy producer --audio-sink wasapisink --output "$d/m5-combined-v2-offsets" *> "$d/m5-combined-v2-offsets.log"
python "$t/m5_renderer_rate_probe.py" --build $b --renderer "$r/Release/vkrenderer.exe" --clock-policy producer --seconds 45 --output "$d/m5-combined-v2-rate" *> "$d/m5-combined-v2-rate.log"
$env:VK_LAYER_PATH="$d/vulkan-validation-local/Bin";$env:VKR_VALIDATION='1'
python "$t/m5_renderer_rate_probe.py" --build $b --renderer "$r/Release/vkrenderer.exe" --clock-policy producer --seconds 10 --case '1920x1080-60-NV12-av' --output "$d/m5-combined-v2-validation" *> "$d/m5-combined-v2-validation.log"
$env:VKR_TEST_GPU_GATE_MS='40';$env:VKR_TEST_AV_IMPORT_FAILURES='1'
python "$t/m5_renderer_rate_probe.py" --build $b --renderer "$r/Release/vkrenderer.exe" --clock-policy producer --seconds 10 --case '1920x1080-60-NV12-av' --output "$d/m5-combined-v2-gpu-stress" *> "$d/m5-combined-v2-gpu-stress.log"
