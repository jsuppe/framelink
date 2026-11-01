// Exercise the real GstAudioBaseSink resampler with a deterministic phase jump.
// A recording ringbuffer replaces hardware; no GPU or sound device is needed.
#include "../samples/audio_alignment.h"
#include <gst/app/gstappsrc.h>
#include <gst/audio/gstaudiobasesink.h>
#include <gst/check/gsttestclock.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct RecordRing {GstAudioRingBuffer parent;};
struct RecordRingClass {GstAudioRingBufferClass parent;};
G_DEFINE_TYPE(RecordRing,record_ring,GST_TYPE_AUDIO_RING_BUFFER)
static std::vector<int> outputs;
static GstClockTime phaseJump;
static void record_ring_init(RecordRing*){}
static void record_ring_class_init(RecordRingClass* c){
 auto* r=GST_AUDIO_RING_BUFFER_CLASS(c);
 r->open_device=[](GstAudioRingBuffer*)->gboolean{return TRUE;};
 r->close_device=[](GstAudioRingBuffer*)->gboolean{return TRUE;};
 r->acquire=[](GstAudioRingBuffer* b,GstAudioRingBufferSpec* s)->gboolean{s->segsize=480*GST_AUDIO_INFO_BPF(&s->info);s->segtotal=20;b->size=s->segsize*s->segtotal;b->memory=static_cast<guint8*>(g_malloc0(b->size));return TRUE;};
 r->release=[](GstAudioRingBuffer* b)->gboolean{g_free(b->memory);b->memory=nullptr;b->size=0;return TRUE;};
 r->start=[](GstAudioRingBuffer*)->gboolean{return TRUE;};r->pause=r->start;r->stop=r->start;r->resume=r->start;
 r->activate=[](GstAudioRingBuffer*,gboolean)->gboolean{return TRUE;};
 r->delay=[](GstAudioRingBuffer*)->guint{return 0;};
 r->commit=[](GstAudioRingBuffer* b,guint64* sample,guint8*,gint input,gint output,gint*)->guint{
  outputs.push_back(output);*sample+=std::abs(output);
  if(outputs.size()==1){
   auto* sink=GST_AUDIO_BASE_SINK(GST_OBJECT_PARENT(b));GstClockTime i,e,n,d;
   // We inject the only calibration change. Do not let an asynchronous master
   // observation race it (especially at one-sample rounding boundaries).
   gst_clock_set_master(sink->provided_clock,nullptr);
   gst_clock_get_calibration(sink->provided_clock,&i,&e,&n,&d);
   gst_clock_set_calibration(sink->provided_clock,i,e+phaseJump,n,d);
  }
  return input;
 };
}
struct RecordSink {GstAudioBaseSink parent;};
struct RecordSinkClass {GstAudioBaseSinkClass parent;};
G_DEFINE_TYPE(RecordSink,record_sink,GST_TYPE_AUDIO_BASE_SINK)
static void record_sink_init(RecordSink*){}
static void record_sink_class_init(RecordSinkClass* c){
 auto* e=GST_ELEMENT_CLASS(c);
 gst_element_class_set_static_metadata(e,"Recording audio sink","Sink/Audio","Alignment regression fixture","FrameLink");
 auto* caps=gst_caps_from_string("audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2");
 gst_element_class_add_pad_template(e,gst_pad_template_new("sink",GST_PAD_SINK,GST_PAD_ALWAYS,caps));gst_caps_unref(caps);
 GST_AUDIO_BASE_SINK_CLASS(c)->create_ringbuffer=[](GstAudioBaseSink*){return GST_AUDIO_RING_BUFFER(g_object_new(record_ring_get_type(),nullptr));};
}
static std::vector<int> run(bool guarded,unsigned samples,GstClockTime jump){
 phaseJump=jump;
 outputs.clear();auto* pipeline=gst_pipeline_new(nullptr);auto* source=gst_element_factory_make("appsrc",nullptr);
 auto* sink=GST_ELEMENT(g_object_new(record_sink_get_type(),"slave-method",0,"async",FALSE,nullptr));
 auto* caps=gst_caps_from_string("audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2");
 g_object_set(source,"caps",caps,"format",GST_FORMAT_TIME,nullptr);gst_caps_unref(caps);
 gst_bin_add_many(GST_BIN(pipeline),source,sink,nullptr);REQUIRE(gst_element_link(source,sink));
 if(guarded)REQUIRE(av::protectAudioAlignment(sink));
 auto* clock=gst_test_clock_new_with_start_time(0);
 gst_pipeline_use_clock(GST_PIPELINE(pipeline),clock);
 REQUIRE(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 std::vector<std::pair<GstBuffer*,guint64>> retained;
 for(unsigned n=0;n<2;++n){
  auto* b=gst_buffer_new_allocate(nullptr,samples*4,nullptr);
  auto pts=GST_SECOND+gst_util_uint64_scale(guint64(n)*samples,GST_SECOND,48000);
  GST_BUFFER_PTS(b)=pts;GST_BUFFER_DURATION(b)=gst_util_uint64_scale(samples,GST_SECOND,48000);
  // Retain a ref to confirm the guard does not rewrite the input buffer.
  gst_buffer_ref(b);REQUIRE(gst_app_src_push_buffer(GST_APP_SRC(source),b)==GST_FLOW_OK);
  retained.emplace_back(b,pts);
 }
 gst_app_src_end_of_stream(GST_APP_SRC(source));
 auto* bus=gst_element_get_bus(pipeline);GstMessage* msg=nullptr;
 const auto deadline=g_get_monotonic_time()+3*G_USEC_PER_SEC;
 while(!msg && g_get_monotonic_time()<deadline){
  msg=gst_bus_pop_filtered(bus,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
  GstClockID id=nullptr;
  if(gst_test_clock_peek_next_pending_id(GST_TEST_CLOCK(clock),&id)){
   auto when=gst_clock_id_get_time(id);
   if(when>gst_clock_get_time(clock))gst_test_clock_set_time(GST_TEST_CLOCK(clock),when);
   gst_test_clock_process_id(GST_TEST_CLOCK(clock),id); // consumes the ID ref
  }else g_usleep(1000);
 }
 if(msg && GST_MESSAGE_TYPE(msg)==GST_MESSAGE_ERROR){GError* e=nullptr;gst_message_parse_error(msg,&e,nullptr);fprintf(stderr,"gst: %s\n",e->message);g_error_free(e);}
 REQUIRE(msg && GST_MESSAGE_TYPE(msg)==GST_MESSAGE_EOS);
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_message_unref(msg);gst_object_unref(bus);gst_object_unref(pipeline);gst_object_unref(clock);
 for(auto& entry:retained){REQUIRE(GST_BUFFER_PTS(entry.first)==entry.second);REQUIRE(GST_BUFFER_DURATION(entry.first)==gst_util_uint64_scale(samples,GST_SECOND,48000));gst_buffer_unref(entry.first);}
 printf("guarded=%d samples=%u jump=%llu commits=%zu",guarded,samples,(unsigned long long)jump,outputs.size());for(auto n:outputs)printf(" %d",n);puts("");
 REQUIRE(outputs.size()==2 && outputs[0]>0);
 if(jump==40*GST_MSECOND)REQUIRE(guarded?outputs[1]>0:outputs[1]<0);
 else REQUIRE(outputs[1]>0);
 return outputs;
}
int main(int argc,char**argv){
 gst_init(&argc,&argv);
 for(unsigned samples:{1600u,200u,1u}){run(false,samples,40*GST_MSECOND);run(true,samples,40*GST_MSECOND);}
 const auto unguarded=run(false,1600,20*GST_USECOND),guarded=run(true,1600,20*GST_USECOND);
 REQUIRE(unguarded==guarded); // small corrections retain normal resampling
 REQUIRE(unguarded[1]!=1600); // the small correction was actually exercised
 puts("real resampler phase-jump negative controls, variable block bounds and small-drift preservation passed");
}
