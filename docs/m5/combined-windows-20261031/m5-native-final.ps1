$env:PATH='C:/Users/jonsu/dev/vkrender-m5-final-validation/build-final/Release;C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
$env:VK_LAYER_PATH='C:/Users/jonsu/dev/vulkan-validation-local/Bin'
$env:VKR_VALIDATION='1'
python C:/Users/jonsu/dev/framelink-m5-final-validation/test/m5_visible_demo.py --build C:/Users/jonsu/dev/vkrender-m5-final-validation/build-final/framelink/Release --renderer C:/Users/jonsu/dev/vkrender-m5-final-validation/build-final/Release/vkrenderer.exe --output C:/Users/jonsu/dev/m5-combined-native-window --port 18865 --renderer-port 18866 --loops 1 *> C:/Users/jonsu/dev/m5-combined-native-window.log
