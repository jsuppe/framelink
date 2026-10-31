$d='C:/Users/jonsu/dev';$r="$d/vkrender-m5-final-validation/build-final";$b="$r/framelink/Release";$env:PATH="$r/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;"+$env:PATH
$env:VK_LAYER_PATH="$d/vulkan-validation-local/Bin";$env:VKR_VALIDATION='1'
python "$d/m5_native_rate_probe.py" --build $b --renderer "$r/Release/vkrenderer.exe" --clock-policy producer --seconds 10 --case '1920x1080-60-NV12-av' --output "$d/m5-combined-native-rate" *> "$d/m5-combined-native-rate.log"
