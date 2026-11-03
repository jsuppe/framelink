$env:PATH='<win-home>/dev/vkrender-m5-final-validation/build-final/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
$env:VK_LAYER_PATH='<win-home>/dev/vulkan-validation-local/Bin'
$env:VKR_VALIDATION='1'
python <win-home>/dev/framelink-m5-final-validation/test/m5_visible_demo.py --build <win-home>/dev/vkrender-m5-final-validation/build-final/framelink/Release --renderer <win-home>/dev/vkrender-m5-final-validation/build-final/Release/vkrenderer.exe --output <win-home>/dev/m5-combined-native-window --port 18865 --renderer-port 18866 --loops 1 *> <win-home>/dev/m5-combined-native-window.log
