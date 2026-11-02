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
 CHECK(!fl_gst_session_new(0)&&!fl_gst_session_new(4));
 for(unsigned tracks=1;tracks<=3;tracks++){
  auto*s=fl_gst_session_new(tracks);auto*cxt=fl_gst_session_create_context(s);
  auto* shared=fl_gst_session_from_context(cxt);CHECK(shared==s);
  g_object_unref(s);gst_context_unref(cxt);s=shared; // full reference survives both releases
  auto c=config(tracks);auto p=offer(c);flSessionConfig out{};out.generation=999;
  CHECK(fl_gst_session_configure(s,&c,&p,&p)==FL_GST_SESSION_READY);
  auto bad=p;bad.version=1;
  CHECK(fl_gst_session_configure(s,&c,&p,&bad)==FL_GST_SESSION_INVALID);
  CHECK(fl_gst_session_wait(s,1,1,&out)==FL_GST_SESSION_TIMEOUT);CHECK(out.generation==999);
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
  CHECK(fl_gst_session_wait(s,1,10,&out)==FL_GST_SESSION_STALE);
  CHECK(fl_gst_session_track_ready(s,tracks&1?1:2,1)==FL_GST_SESSION_STALE);
  std::atomic<bool> entered{false};FlGstSessionResult result=FL_GST_SESSION_READY;
  std::thread waiter([&]{entered=true;result=fl_gst_session_wait(s,2,5000,&out);});
  while(!entered.load())std::this_thread::yield();
  auto start=g_get_monotonic_time();fl_gst_session_cancel(s);waiter.join();
  CHECK(result==FL_GST_SESSION_CANCELLED);CHECK(g_get_monotonic_time()-start<1000000);
  g_object_unref(s);
 }
 auto*wrong=gst_context_new(FL_GST_SESSION_CONTEXT,TRUE);
 gst_structure_set(gst_context_writable_structure(wrong),"coordinator",G_TYPE_STRING,"not an object",nullptr);
 CHECK(!fl_gst_session_from_context(wrong));gst_context_unref(wrong);
 puts("session context: ownership, readiness, track masks, generations and cancellation passed");
}
