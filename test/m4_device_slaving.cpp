// Optional Linux virtual-device mechanism qualification, not physical A/V sync.
// Run only against a dedicated/null PulseAudio sink; not part of hosted CTest.
#include <gst/gst.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct Observations { std::atomic<unsigned> slaving{0}, corrected{0}; };
static void observe(GstDebugCategory*,GstDebugLevel,const gchar*,const gchar* function,
                    gint,GObject*,GstDebugMessage* message,gpointer data) {
  if(!function || strcmp(function,"gst_audio_base_sink_resample_slaving"))return;
  const char* text=gst_debug_message_get(message);
  const char* rate=strstr(text," = ");if(!rate)return;
  auto* o=static_cast<Observations*>(data);++o->slaving;
  double ratio=strtod(rate+3,nullptr);
  if(std::isfinite(ratio) && std::abs(ratio-1.0)>0.000001)++o->corrected;
}
int main(int argc,char**argv) {
  gst_init(&argc,&argv);
  const bool negative=argc>1 && !strcmp(argv[1],"--no-resample");
  Observations counts;
  gst_debug_set_threshold_for_name("audiobasesink",GST_LEVEL_DEBUG);
  gst_debug_add_log_function(observe,&counts,nullptr);
  GError* error=nullptr;
  const char* positive="audiotestsrc is-live=true samplesperbuffer=480 num-buffers=600 wave=sine volume=0.001 ! audio/x-raw,rate=48000,channels=2 ! pulsesink provide-clock=false slave-method=resample sync=true";
  const char* control="audiotestsrc is-live=true samplesperbuffer=480 num-buffers=600 wave=sine volume=0.001 ! audio/x-raw,rate=48000,channels=2 ! pulsesink provide-clock=false slave-method=none sync=true";
  auto* pipeline=gst_parse_launch(negative?control:positive,&error);
  if(!pipeline || error){fprintf(stderr,"pipeline: %s\n",error?error->message:"unavailable");return 2;}
  auto* clock=GST_CLOCK(g_object_new(GST_TYPE_SYSTEM_CLOCK,nullptr));
  gst_pipeline_use_clock(GST_PIPELINE(pipeline),clock);
  gst_element_set_state(pipeline,GST_STATE_PLAYING);
  g_usleep(1000000);
  auto calibrate=[&](guint64 rate){
    const auto internal=gst_clock_get_internal_time(clock), external=gst_clock_get_time(clock);
    gst_clock_set_calibration(clock,internal,external,rate,1000000);
  };
  calibrate(1001000);g_usleep(2000000);calibrate(999000);
  auto* bus=gst_element_get_bus(pipeline);
  auto* message=gst_bus_timed_pop_filtered(bus,8*GST_SECOND,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));
  bool passed=message && GST_MESSAGE_TYPE(message)==GST_MESSAGE_EOS;
  gst_element_set_state(pipeline,GST_STATE_NULL);
  gst_debug_remove_log_function(observe);
  passed=passed && (negative ? counts.slaving==0 : counts.corrected>10);
  printf("device-slaving control=%d callbacks=%u nonunity=%u passed=%d\n",negative,counts.slaving.load(),counts.corrected.load(),passed);
  if(message)gst_message_unref(message);gst_object_unref(bus);gst_object_unref(clock);gst_object_unref(pipeline);
  return passed?0:1;
}
