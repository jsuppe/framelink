#include "framelink_session_context.h"
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static flSessionConfig config(unsigned tracks) {
 flSessionConfig c{};c.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;c.sessionId.bytes[0]=1;
 c.clockId.bytes[0]=2;c.generation=1;c.epochNs=37000000;c.connectTimeoutMs=100;c.idleTimeoutMs=100;
 if(tracks&1){c.videoTrackId=1;c.video={FL_FORMAT_RGBA8,16,16,60,1};c.videoQueueFrames=2;}
 if(tracks&2){c.audioTrackId=2;c.audio={FL_PCM_S16,48000,FL_LAYOUT_STEREO};c.audioQueueMs=80;}
 return c;
}
static flSessionCapabilities offer(const flSessionConfig& c){
 flSessionCapabilities o{};o.version=c.version;
 if(c.videoTrackId){o.videoCount=1;o.video[0]=c.video;}
 if(c.audioTrackId){o.audioCount=1;o.audio[0]=c.audio;}return o;
}
int main(int argc,char**argv){
 gst_init(&argc,&argv);
 GstSegment seg;gst_segment_init(&seg,GST_FORMAT_TIME);seg.start=2*GST_SECOND;seg.base=500000000;
 GstClockTime converted=999;CHECK(fl_gst_segment_running_time(&seg,2*GST_SECOND+37000000,&converted)&&converted==537000000);
 converted=999;CHECK(!fl_gst_segment_running_time(&seg,0,&converted)&&converted==999);seg.rate=2;CHECK(!fl_gst_segment_running_time(&seg,2*GST_SECOND,&converted));seg.rate=1;CHECK(!fl_gst_segment_running_time(&seg,GST_CLOCK_TIME_NONE,&converted));
 CHECK(!fl_gst_session_new(0)&&!fl_gst_session_new(4));
 for(unsigned tracks=1;tracks<=3;tracks++){
  auto*s=fl_gst_session_new(tracks);auto*cxt=fl_gst_session_create_context(s);
  auto* shared=fl_gst_session_from_context(cxt);CHECK(shared==s);
  g_object_unref(s);gst_context_unref(cxt);s=shared; // full reference survives both releases
  auto c=config(tracks);auto p=offer(c);flSessionConfig out{};out.generation=999;
  CHECK(fl_gst_session_configure(s,&c,&p,&p)==FL_GST_SESSION_READY);
  gint64 identity=-1;CHECK(fl_gst_session_map_running_time(s,1,37000000,FALSE,&identity)==FL_GST_SESSION_READY&&identity==37000000);CHECK(fl_gst_session_map_running_time(s,1,37000000,TRUE,&identity)==FL_GST_SESSION_INVALID);
  flClockMapping mapping{};mapping.sourceClockId=c.clockId;mapping.targetClockId.bytes[0]=8;
  mapping.sourceAnchorNs=c.epochNs;mapping.targetAnchorNs=1000000000;
  CHECK(fl_gst_session_set_mapping(s,1,&mapping,0)==FL_GST_SESSION_READY);
  gint64 audio=0,video=0;
  CHECK(fl_gst_session_map_pts(s,1,0,&audio)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_map_pts(s,1,37000000,&video)==FL_GST_SESSION_READY);
  CHECK(video-audio==37000000&&audio==1000000000);
  auto changed=mapping;changed.targetAnchorNs++;
  CHECK(fl_gst_session_set_mapping(s,1,&changed,0)==FL_GST_SESSION_INVALID);
  gint64 untouched=123;CHECK(fl_gst_session_map_pts(s,1,-1,&untouched)==FL_GST_SESSION_INVALID);CHECK(untouched==123);
  auto bad=p;bad.version=1;
  CHECK(fl_gst_session_configure(s,&c,&p,&bad)==FL_GST_SESSION_INVALID);
  CHECK(fl_gst_session_wait(s,1,1,&out)==FL_GST_SESSION_TIMEOUT);CHECK(out.generation==999);
  CHECK(fl_gst_session_get_config(s,&out)==FL_GST_SESSION_READY);CHECK(out.generation==1);
  CHECK(fl_gst_session_track_ready(s,tracks&1?1:2,1)==FL_GST_SESSION_INVALID);
  for(unsigned track=1;track<=2;track++)if(tracks&track){
   CHECK(fl_gst_session_register_track(s,track)==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_register_track(s,track)==FL_GST_SESSION_INVALID);
   CHECK(fl_gst_session_track_ready(s,track,1)==FL_GST_SESSION_READY);
   if(tracks==3&&track==1)CHECK(fl_gst_session_wait(s,1,1,&out)==FL_GST_SESSION_TIMEOUT);
  }
  CHECK(fl_gst_session_wait(s,1,10,&out)==FL_GST_SESSION_READY);CHECK(out.epochNs==37000000);
  CHECK(fl_gst_session_configure(s,&c,&p,&p)==FL_GST_SESSION_STALE);
  fl_gst_session_cancel(s);CHECK(fl_gst_session_wait(s,1,10,&out)==FL_GST_SESSION_CANCELLED);
  c.generation=2;CHECK(fl_gst_session_configure(s,&c,&p,&p)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_map_running_time(s,2,37000000,TRUE,&identity)==FL_GST_SESSION_INVALID);
  CHECK(fl_gst_session_set_mapping(s,1,&mapping,0)==FL_GST_SESSION_STALE);
  mapping.targetAnchorNs+=500000000;
  CHECK(fl_gst_session_set_mapping(s,2,&mapping,0)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_map_pts(s,2,0,&audio)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_map_pts(s,2,37000000,&video)==FL_GST_SESSION_READY);
  CHECK(video-audio==37000000&&audio==1500000000);
  CHECK(fl_gst_session_wait(s,1,10,&out)==FL_GST_SESSION_STALE);
  CHECK(fl_gst_session_track_ready(s,tracks&1?1:2,1)==FL_GST_SESSION_STALE);
  std::atomic<bool> entered{false};FlGstSessionResult result=FL_GST_SESSION_READY;
  std::thread waiter([&]{entered=true;result=fl_gst_session_wait(s,2,5000,&out);});
  while(!entered.load())std::this_thread::yield();
  auto start=g_get_monotonic_time();fl_gst_session_cancel(s);waiter.join();
  CHECK(result==FL_GST_SESSION_CANCELLED);CHECK(g_get_monotonic_time()-start<1000000);
  g_object_unref(s);
 }
 // Standard bin context propagation supplies one owning object to both children.
 auto* sharedSession=fl_gst_session_new(3);auto* context=fl_gst_session_create_context(sharedSession);
 auto* pipeline=gst_pipeline_new(nullptr);auto* one=gst_bin_new(nullptr);auto* two=gst_bin_new(nullptr);
 gst_bin_add_many(GST_BIN(pipeline),one,two,nullptr);gst_element_set_context(pipeline,context);
 auto* childContext=gst_element_get_context(one,FL_GST_SESSION_CONTEXT);CHECK(childContext);
 auto* fromChild=fl_gst_session_from_context(childContext);CHECK(fromChild==sharedSession);
 g_object_unref(fromChild);gst_context_unref(childContext);
 childContext=gst_element_get_context(two,FL_GST_SESSION_CONTEXT);CHECK(childContext);
 fromChild=fl_gst_session_from_context(childContext);CHECK(fromChild==sharedSession);
 g_object_unref(fromChild);gst_context_unref(childContext);
 gst_object_unref(pipeline);gst_context_unref(context);g_object_unref(sharedSession);
 auto*wrong=gst_context_new(FL_GST_SESSION_CONTEXT,TRUE);
 gst_structure_set(gst_context_writable_structure(wrong),"coordinator",G_TYPE_STRING,"not an object",nullptr);
 CHECK(!fl_gst_session_from_context(wrong));gst_context_unref(wrong);
 puts("session context: ownership, readiness, track masks, generations and cancellation passed");
}
