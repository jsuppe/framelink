// Public two-process A/V setup helper, exercised in one process with two
// threads so both roles use real pipelines: authenticated rendezvous, clock
// mapping, audio/event streams, video lease, pipeline clocks and live media.
#include "framelink_session_transport.h"
#include <gst/app/gstappsink.h>
#include <algorithm>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);GST_PLUGIN_STATIC_DECLARE(videotestsrc);GST_PLUGIN_STATIC_DECLARE(audiotestsrc);
}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static GstElement* parse(const char*text){GError*e=nullptr;auto*p=gst_parse_launch(text,&e);if(e)fprintf(stderr,"parse: %s\n",e->message);CHECK(p&&!e);return p;}
static flSessionCapabilities caps(bool video,bool audio){
 flSessionCapabilities c{};c.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 if(video){c.videoCount=1;c.video[0]={FL_FORMAT_RGBA8,64,32,30,1};}
 if(audio){c.audioCount=1;c.audio[0]={FL_PCM_S16,48000,2};}
 return c;
}
static std::string sessionPath(){gchar*root=g_dir_make_tmp("framelink-av-XXXXXX",nullptr);CHECK(root);std::string p=std::string(root)+"/session";g_free(root);return p;}
static void mismatch(){
 auto path=sessionPath();auto*p=fl_gst_session_new(3);auto*c=fl_gst_session_new(2);auto pc=caps(true,true),cc=caps(false,true);
 gboolean pr=TRUE,cr=TRUE;flChannel*pch=nullptr,*cch=nullptr;GError*pe=nullptr,*ce=nullptr;auto begin=g_get_monotonic_time();
 std::thread a([&]{pr=fl_gst_session_open_local_av(p,TRUE,path.c_str(),&pc,2,0,80,2000,2000,0,&pch,nullptr,&pe);});
 std::thread b([&]{cr=fl_gst_session_open_local_av(c,FALSE,path.c_str(),&cc,2,0,80,2000,2000,0,&cch,nullptr,&ce);});a.join();b.join();
 fprintf(stderr,"track mismatch: producer %s; consumer %s\n",pe?pe->message:"ok",ce?ce->message:"ok");
 CHECK(!pr&&!cr&&!pch&&!cch&&g_get_monotonic_time()-begin<3000000);
 flSessionConfig out{};CHECK(fl_gst_session_get_config(p,&out)!=FL_GST_SESSION_READY&&fl_gst_session_get_config(c,&out)!=FL_GST_SESSION_READY);
 g_clear_error(&pe);g_clear_error(&ce);g_object_unref(p);g_object_unref(c);
}
static void queueCapacity(){
 for(guint depth:{1u,2u}){
  auto path=sessionPath();auto*p=fl_gst_session_new(3);auto*c=fl_gst_session_new(3);auto local=caps(true,true);
  gboolean pr=FALSE,cr=FALSE;flChannel*pch=nullptr,*cch=nullptr;GError*pe=nullptr,*ce=nullptr;
  std::thread a([&]{pr=fl_gst_session_open_local_av(p,TRUE,path.c_str(),&local,depth,0,80,2000,2000,0,&pch,nullptr,&pe);});
  std::thread b([&]{cr=fl_gst_session_open_local_av(c,FALSE,path.c_str(),&local,depth,2,80,2000,2000,0,&cch,nullptr,&ce);});a.join();b.join();
  if(depth==1){CHECK(pr&&cr&&pch&&cch);flSessionConfig cfg{};CHECK(fl_gst_session_get_config(c,&cfg)==FL_GST_SESSION_READY&&cfg.videoQueueFrames==1);}
  else{CHECK(!pr&&!cr&&!pch&&!cch&&ce&&std::string(ce->message).find("queue exceeds ring capacity")!=std::string::npos);}
  if(pch)flClose(pch);if(cch)flClose(cch);fl_gst_session_cancel(p);fl_gst_session_cancel(c);
  g_clear_error(&pe);g_clear_error(&ce);g_object_unref(p);g_object_unref(c);
 }
}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#else
 CHECK(argc>1);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 mismatch();queueCapacity();
 for(int consumerFirst=0;consumerFirst<2;consumerFirst++){
  auto path=sessionPath();auto*sender=fl_gst_session_new(3);auto*receiver=fl_gst_session_new(3);auto local=caps(true,true);
  gboolean pr=FALSE,cr=FALSE;flChannel*producer=nullptr,*consumer=nullptr;GError*pe=nullptr,*ce=nullptr;
  auto p=[&]{pr=fl_gst_session_open_local_av(sender,TRUE,path.c_str(),&local,2,8,80,3000,3000,0,&producer,nullptr,&pe);};
  auto c=[&]{cr=fl_gst_session_open_local_av(receiver,FALSE,path.c_str(),&local,2,8,80,3000,3000,0,&consumer,nullptr,&ce);};
  std::thread a([&]{if(consumerFirst)c();else p();});g_usleep(30000);std::thread b([&]{if(consumerFirst)p();else c();});a.join();b.join();
  if(pe)fprintf(stderr,"producer: %s\n",pe->message);if(ce)fprintf(stderr,"consumer: %s\n",ce->message);
  CHECK(pr&&cr&&producer&&consumer);
  flSessionConfig cfg{};CHECK(fl_gst_session_get_config(receiver,&cfg)==FL_GST_SESSION_READY&&cfg.videoTrackId&&cfg.audioTrackId&&cfg.generation==1);
  // One process, one monotonic clock: the measured mapping must be identity within its uncertainty.
  gint64 mapped=0;CHECK(fl_gst_session_map_running_time(receiver,1,0,TRUE,&mapped)==FL_GST_SESSION_READY);
  const gint64 skew=mapped-cfg.epochNs;fprintf(stderr,"mapping skew %" G_GINT64_FORMAT "ns\n",skew);CHECK(skew>-5000000&&skew<5000000);
  auto*tx=parse("videotestsrc is-live=true num-buffers=30 pattern=red ! video/x-raw,format=RGBA,width=64,height=32,framerate=30/1 ! framelinksink name=video "
                "audiotestsrc is-live=true num-buffers=50 samplesperbuffer=480 wave=sine ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! framelinkaudiosink tracks=av");
  auto*rx=parse("framelinksrc name=video width=64 height=32 format=RGBA ! queue ! appsink name=vout sync=true max-buffers=0 framelinkaudiosrc tracks=av ! queue ! appsink name=aout sync=true max-buffers=0");
  for(auto pair:{std::make_pair(tx,sender),std::make_pair(rx,receiver)}){auto*context=fl_gst_session_create_context(pair.second);gst_element_set_context(pair.first,context);gst_context_unref(context);}
  auto*vs=gst_bin_get_by_name(GST_BIN(tx),"video");auto*vr=gst_bin_get_by_name(GST_BIN(rx),"video");gboolean adopted=FALSE;
  g_signal_emit_by_name(vs,"adopt-channel",producer,&adopted);CHECK(adopted);g_signal_emit_by_name(vr,"adopt-channel",consumer,&adopted);CHECK(adopted);
  const GstClockTime latency=150*GST_MSECOND;GError*clockError=nullptr;
  CHECK(fl_gst_session_apply_pipeline_clock(receiver,GST_PIPELINE(rx),FALSE,latency,&clockError));
  CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  CHECK(fl_gst_session_apply_pipeline_clock(sender,GST_PIPELINE(tx),TRUE,0,&clockError));
  CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  auto*vout=gst_bin_get_by_name(GST_BIN(rx),"vout");auto*aout=gst_bin_get_by_name(GST_BIN(rx),"aout");
  auto*clock=gst_system_clock_obtain();guint videos=0;guint64 samples=0;GstClockTime firstV=GST_CLOCK_TIME_NONE,firstA=GST_CLOCK_TIME_NONE;
  gint64 worst[2]={G_MININT64,G_MININT64},best[2]={G_MAXINT64,G_MAXINT64};bool done[2]={false,false};
  // One puller per sink so pulling never delays the other track's measurement.
  auto pull=[&](GstElement*sink,int t){
   auto deadline=g_get_monotonic_time()+8*G_USEC_PER_SEC;
   while(g_get_monotonic_time()<deadline){
    auto*smp=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),100*GST_MSECOND);
    if(!smp){if(gst_app_sink_is_eos(GST_APP_SINK(sink))){done[t]=true;return;}continue;}
    auto*b=gst_sample_get_buffer(smp);const auto now=gst_clock_get_time(clock);
    // sync=true renders at PTS+latency on the receiver system clock.
    const gint64 lag=gint64(now)-gint64(GST_BUFFER_PTS(b));worst[t]=std::max(worst[t],lag);best[t]=std::min(best[t],lag);
    if(!t){if(!videos)firstV=GST_BUFFER_PTS(b);++videos;GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_READ));for(gsize i=0;i<m.size;i+=4)CHECK(m.data[i]==255&&m.data[i+1]==0&&m.data[i+2]==0&&m.data[i+3]==255);gst_buffer_unmap(b,&m);}
    else{if(!samples)firstA=GST_BUFFER_PTS(b);samples+=gst_buffer_get_size(b)/4;}
    gst_sample_unref(smp);
   }};
  std::thread pv([&]{pull(vout,0);}),pa([&]{pull(aout,1);});pv.join();pa.join();
  for(int t=0;t<2;t++){fprintf(stderr,"%s lag %.1f..%.1fms\n",t?"audio":"video",best[t]/1e6,worst[t]/1e6);CHECK(best[t]>=gint64(latency)-5000000&&worst[t]<=gint64(latency)+100000000);}
  const bool vdone=done[0],adone=done[1];
  fprintf(stderr,"order=%s video=%u samples=%" G_GUINT64_FORMAT " firstAV=%" G_GINT64_FORMAT "ns\n",consumerFirst?"consumer-first":"producer-first",videos,samples,gint64(firstA)-gint64(firstV));
  if(videos!=30){for(auto*pl:{tx,rx}){auto*bus=gst_element_get_bus(pl);while(auto*m=gst_bus_pop(bus)){if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ELEMENT||GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR||GST_MESSAGE_TYPE(m)==GST_MESSAGE_WARNING||GST_MESSAGE_TYPE(m)==GST_MESSAGE_QOS){auto*st=gst_message_get_structure(m);gchar*t=st?gst_structure_to_string(st):g_strdup("-");fprintf(stderr,"%s %s: %.300s\n",GST_MESSAGE_TYPE_NAME(m),GST_OBJECT_NAME(m->src),t);g_free(t);}gst_message_unref(m);}gst_object_unref(bus);}}
  CHECK(vdone&&adone&&videos==30&&samples==50*480);
  // Both live sources start at the same producer running time; mapping is shared.
  CHECK(llabs(gint64(firstA)-gint64(firstV))<40000000);
  gst_object_unref(clock);gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);
  for(auto*e:{vs,vr,vout,aout,tx,rx})gst_object_unref(e);g_object_unref(sender);g_object_unref(receiver);
 }
 puts("local A/V setup: authenticated rendezvous, identity clock mapping, both startup orders, live synchronized pixels/PCM, track mismatch rejection passed");
 return 0;
}
