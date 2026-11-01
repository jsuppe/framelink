#pragma once
#include <mutex>
#include <gst/gst.h>
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);
GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);
GST_PLUGIN_STATIC_DECLARE(tcp);
GST_PLUGIN_STATIC_DECLARE(gio);
GST_PLUGIN_STATIC_DECLARE(audioconvert);
GST_PLUGIN_STATIC_DECLARE(audioresample);
GST_PLUGIN_STATIC_DECLARE(volume);
GST_PLUGIN_STATIC_DECLARE(opensles);
}
inline void flRegisterAndroidPlugins() {
 static std::once_flag once;
 std::call_once(once, [] {
 GST_PLUGIN_STATIC_REGISTER(framelink);
 GST_PLUGIN_STATIC_REGISTER(app);
 GST_PLUGIN_STATIC_REGISTER(coreelements);
 GST_PLUGIN_STATIC_REGISTER(gdp);
 GST_PLUGIN_STATIC_REGISTER(tcp);
 GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(audioconvert);
 GST_PLUGIN_STATIC_REGISTER(audioresample);
 GST_PLUGIN_STATIC_REGISTER(volume);
 GST_PLUGIN_STATIC_REGISTER(opensles);
 });
}
