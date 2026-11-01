$d='C:/Users/jonsu/dev';$r="$d/vkrender-m5-final-validation/build-final";$b="$r/framelink/Release";$t="$d/framelink-m5-final-validation/test";$env:PATH="$r/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;"+$env:PATH
$env:VK_LAYER_PATH="$d/vulkan-validation-local/Bin";$env:VKR_VALIDATION='1';$env:VK_LOADER_DEBUG='error,warn,layer'
python "$t/m5_renderer_rate_probe.py" --build $b --renderer "$r/Release/vkrenderer.exe" --clock-policy producer --seconds 10 --case '1920x1080-60-NV12-av' --output "$d/m5-combined-limited-validation" *> "$d/m5-combined-limited-validation.log"
$env:VKR_TEST_GPU_GATE_MS='40';$env:VKR_TEST_AV_IMPORT_FAILURES='1'
python "$t/m5_renderer_rate_probe.py" --build $b --renderer "$r/Release/vkrenderer.exe" --clock-policy producer --seconds 10 --case '1920x1080-60-NV12-av' --output "$d/m5-combined-limited-stress" *> "$d/m5-combined-limited-stress.log"
