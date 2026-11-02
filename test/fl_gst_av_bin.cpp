// framelinkavsink (M5b task9): the producer bin with session= against the
// validated task7 consumer path (fl_gst_session_open_local_av + elements).
// Live producers start PLAYING before or after the consumer exists; media is
// held, released on one shared timeline and drained before producer EOS.
#include "framelink_session_transport.h"
#include <gst/app/gstappsink.h>
#include <gst/check/gsttestclock.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);GST_PLUGIN_STATIC_DECLARE(videotestsrc);GST_PLUGIN_STATIC_DECLARE(audiotestsrc);
}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static constexpr guint FRAMES=30,BLOCKS=50,BLOCK=480;static constexpr GstClockTime LATENCY=150*GST_MSECOND;
static GstElement* parse(const std::string&text){GError*e=nullptr;auto*p=gst_parse_launch(text.c_str(),&e);if(e)fprintf(stderr,"parse: %s\n",e->message);CHECK(p&&!e);return p;}
static std::string sessionPath(){gchar*root=g_dir_make_tmp("framelink-avbin-XXXXXX",nullptr);CHECK(root);std::string p=std::string(root)+"/session";g_free(root);return p;}
static std::string errorText(GstMessage*m){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);std::string t=std::string(GST_OBJECT_NAME(m->src))+": "+e->message;g_clear_error(&e);g_free(d);return t;}
static GstMessage* finish(GstElement*p,GstClockTime timeout){auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,timeout,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));gst_object_unref(bus);return m;}

struct Consumer {
 std::string tracks;std::atomic<guint> frames{0};guint64 samples=0;gint64 worst=G_MININT64,best=G_MAXINT64,trackWorst[2]={G_MININT64,G_MININT64};bool uniform=true,contiguous=true;GstClockTime next=GST_CLOCK_TIME_NONE;
 bool ok=false;std::string error;guint abortAfter=0;
 void lag(GstElement*sink,GstBuffer*b,int track){auto*clock=gst_element_get_clock(sink);if(!clock)return;
  gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(sink))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
  worst=std::max(worst,l);best=std::min(best,l);trackWorst[track]=std::max(trackWorst[track],l);}
 void run(const std::string&path,bool video,bool audio){
  auto*session=fl_gst_session_new((video?1u:0u)|(audio?2u:0u));flSessionCapabilities c{};c.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
  if(video){c.videoCount=1;c.video[0]={FL_FORMAT_RGBA8,64,32,30,1};}if(audio){c.audioCount=1;c.audio[0]={FL_PCM_S16,48000,2};}
  GError*e=nullptr;flChannel*channel=nullptr;
  if(!fl_gst_session_open_local_av(session,FALSE,path.c_str(),&c,2,8,80,5000,5000,0,&channel,nullptr,&e)){error=std::string("setup: ")+e->message;g_clear_error(&e);g_object_unref(session);return;}
  std::string text;
  if(video)text+="framelinksrc name=video width=64 height=32 format=RGBA ! queue ! appsink name=vout sync=true max-buffers=0 ";
  if(audio)text+=std::string("framelinkaudiosrc tracks=")+(video?"av":"audio")+" ! queue ! appsink name=aout sync=true max-buffers=0";
  auto*p=parse(text);auto*context=fl_gst_session_create_context(session);gst_element_set_context(p,context);gst_context_unref(context);
  if(channel){auto*v=gst_bin_get_by_name(GST_BIN(p),"video");gboolean adopted=FALSE;g_signal_emit_by_name(v,"adopt-channel",channel,&adopted);gst_object_unref(v);CHECK(adopted);}
  for(const char*name:{"vout","aout"}){auto*sink=gst_bin_get_by_name(GST_BIN(p),name);if(!sink)continue;
   GstAppSinkCallbacks cb{};cb.new_sample=[](GstAppSink*s,gpointer d)->GstFlowReturn{auto*self=static_cast<Consumer*>(d);auto*sample=gst_app_sink_pull_sample(s);auto*b=gst_sample_get_buffer(sample);
     self->lag(GST_ELEMENT(s),b,strcmp(GST_OBJECT_NAME(s),"vout")?1:0);
     if(!strcmp(GST_OBJECT_NAME(s),"vout")){GstMapInfo m{};gst_buffer_map(b,&m,GST_MAP_READ);for(gsize i=4;i+3<m.size;i+=4)if(memcmp(m.data+i,m.data,4)){self->uniform=false;break;}gst_buffer_unmap(b,&m);self->frames++;}
     else{if(GST_CLOCK_TIME_IS_VALID(self->next)){auto d=gint64(GST_BUFFER_PTS(b))-gint64(self->next);if(d<-1||d>1)self->contiguous=false;}self->next=GST_BUFFER_PTS(b)+GST_BUFFER_DURATION(b);self->samples+=gst_buffer_get_size(b)/4;}
     gst_sample_unref(sample);return GST_FLOW_OK;};
   gst_app_sink_set_callbacks(GST_APP_SINK(sink),&cb,this,nullptr);gst_object_unref(sink);}
  if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),FALSE,LATENCY,&e)){error=e->message;g_clear_error(&e);}
  else if(gst_element_set_state(p,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)error="consumer start";
  else if(!fl_gst_session_local_ready(session,FALSE,5000,&e)){error=e->message;g_clear_error(&e);}
  else if(abortAfter){
   // Consumer death: stop abruptly mid-stream, no drain or EOS.
   auto until=g_get_monotonic_time()+10000000;while(frames.load()<abortAfter&&g_get_monotonic_time()<until)g_usleep(5000);error="aborted";
  }else{auto*m=finish(p,20*GST_SECOND);if(!m)error="consumer EOS timeout";else if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR)error=errorText(m);else ok=true;if(m)gst_message_unref(m);
   if(ok&&!fl_gst_session_finish_local(session,FALSE,5000,&e)){error=e->message;g_clear_error(&e);ok=false;}}
  gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);g_object_unref(session);
 }
};

static std::string producerText(const std::string&path,const std::string&tracks,const std::string&extra="",bool paced=false){
 // paced: non-live sources timed by clocksync; their timestamps stay contiguous
 // across a pause (live audiotestsrc skips ~20ms on resume without DISCONT,
 // which framelinkaudiosink rightly refuses as an unannounced gap).
 const std::string live=paced?"false":"true",sync=paced?" ! clocksync":"";
 (void)path; // set by producerPipeline(): launch syntax would eat Windows backslashes
 std::string t="framelinkavsink name=s tracks="+tracks+" video-format=RGBA width=64 height=32 framerate=30/1 connect-timeout-ms=5000 "+extra+" ";
 if(tracks!="audio")t+="videotestsrc is-live="+live+" num-buffers="+std::to_string(FRAMES)+" pattern=red ! video/x-raw,format=RGBA,width=64,height=32,framerate=30/1"+sync+" ! s.video_sink ";
 if(tracks!="video")t+="audiotestsrc is-live="+live+" num-buffers="+std::to_string(BLOCKS)+" samplesperbuffer="+std::to_string(BLOCK)+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2"+sync+" ! s.audio_sink";
 return t;
}
static GstElement* producerPipeline(const std::string&text,const std::string&path){
 auto*p=parse(text);auto*s=gst_bin_get_by_name(GST_BIN(p),"s");CHECK(s);g_object_set(s,"session",path.c_str(),nullptr);gst_object_unref(s);return p;
}

static void pair(const std::string&tracks,bool producerFirst,guint producerLeadMs){
 fprintf(stderr,"CASE tracks=%s producer-first=%d lead=%ums\n",tracks.c_str(),producerFirst,producerLeadMs);
 const bool video=tracks!="audio",audio=tracks!="video";auto path=sessionPath();
 auto*p=producerPipeline(producerText(path,tracks),path);Consumer c;std::thread consumer;
 if(!producerFirst){consumer=std::thread([&]{c.run(path,video,audio);});g_usleep(200000);}
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 if(producerFirst){g_usleep(producerLeadMs*1000);consumer=std::thread([&]{c.run(path,video,audio);});}
 auto*m=finish(p,25*GST_SECOND);std::string perr=!m?"producer EOS timeout":GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR?errorText(m):"";if(m)gst_message_unref(m);
 consumer.join();
 auto*s=gst_bin_get_by_name(GST_BIN(p),"s");guint64 generation=0;guint vid=0,aid=0;gchar*id=nullptr;
 g_object_get(s,"generation",&generation,"video-track-id",&vid,"audio-track-id",&aid,"session-id",&id,nullptr);gst_object_unref(s);
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);
 fprintf(stderr,"producer=%s consumer=%s video=%u samples=%" G_GUINT64_FORMAT " lag=%.1f..%.1fms generation=%" G_GUINT64_FORMAT " ids=%u/%u session=%s\n",
  perr.empty()?"ok":perr.c_str(),c.ok?"ok":c.error.c_str(),c.frames.load(),c.samples,c.best/1e6,c.worst/1e6,generation,vid,aid,id?id:"-");
 CHECK(perr.empty()&&c.ok);
 CHECK(c.frames==(video?FRAMES:0u)&&c.samples==(audio?guint64(BLOCKS)*BLOCK:0u)&&c.uniform&&c.contiguous);
 // The shared offset puts the producer timeline on the session epoch: every
 // buffer renders one consumer latency after capture, on both tracks.
 CHECK(c.best>=gint64(LATENCY)-5*GST_MSECOND&&c.worst<=gint64(LATENCY)+100*GST_MSECOND);
 CHECK(generation==1&&id&&strlen(id)==32&&(vid!=0)==video&&(aid!=0)==audio);g_free(id);
}

static void negative(const char*name,const std::string&tracks,const std::string&consumerTracks,bool testClock,guint connectMs,const char*expected){
 fprintf(stderr,"CASE negative %s\n",name);
 auto path=sessionPath();auto*p=producerPipeline(producerText(path,tracks,"connect-timeout-ms="+std::to_string(connectMs)),path);
 GstClock*clock=nullptr;if(testClock){clock=gst_test_clock_new();gst_pipeline_use_clock(GST_PIPELINE(p),clock);}
 Consumer c;std::thread consumer;if(!consumerTracks.empty())consumer=std::thread([&]{c.run(path,consumerTracks!="audio",consumerTracks!="video");});
 // A refused clock fails the state change itself; other failures arrive later.
 auto begin=g_get_monotonic_time();const bool started=gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE;CHECK(started||testClock);
 auto*m=finish(p,GstClockTime(3ull*connectMs+5000)*GST_MSECOND);CHECK(m);const bool error=GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR;std::string text=error?errorText(m):"EOS";gst_message_unref(m);
 const gint64 took=g_get_monotonic_time()-begin;
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);if(clock)gst_object_unref(clock);if(consumer.joinable())consumer.join();
 fprintf(stderr,"  producer error after %.0fms: %s; consumer: %s\n",took/1e3,text.c_str(),c.ok?"ok":c.error.c_str());
 CHECK(error&&text.find(expected)!=std::string::npos&&!c.ok&&took<gint64(2ull*connectMs+3000)*1000);
}

// Thin composition: the application owns setup, clock and channel adoption.
static void thin(){
 fprintf(stderr,"CASE thin (application-installed context)\n");
 auto path=sessionPath();Consumer c;std::thread consumer([&]{c.run(path,true,true);});
 auto*session=fl_gst_session_new(3);flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;caps.videoCount=caps.audioCount=1;
 caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};caps.audio[0]={FL_PCM_S16,48000,2};GError*e=nullptr;flChannel*channel=nullptr;
 CHECK(fl_gst_session_open_local_av(session,TRUE,path.c_str(),&caps,2,0,80,5000,5000,0,&channel,nullptr,&e));
 std::string text=producerText(path,"av");
 auto*p=parse(text);auto*context=fl_gst_session_create_context(session);gst_element_set_context(p,context);gst_context_unref(context);
 auto*s=gst_bin_get_by_name(GST_BIN(p),"s");gboolean adopted=FALSE;g_signal_emit_by_name(s,"adopt-channel",channel,&adopted);gst_object_unref(s);CHECK(adopted);
 CHECK(fl_gst_session_local_ready(session,TRUE,5000,&e));CHECK(fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),TRUE,0,&e));
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*m=finish(p,20*GST_SECOND);CHECK(m);if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR)fprintf(stderr,"%s\n",errorText(m).c_str());CHECK(GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS);gst_message_unref(m);
 CHECK(fl_gst_session_finish_local(session,TRUE,5000,&e));consumer.join();
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);g_object_unref(session);
 fprintf(stderr,"consumer=%s video=%u samples=%" G_GUINT64_FORMAT " lag=%.1f..%.1fms\n",c.ok?"ok":c.error.c_str(),c.frames.load(),c.samples,c.best/1e6,c.worst/1e6);
 CHECK(c.ok&&c.frames==FRAMES&&c.samples==guint64(BLOCKS)*BLOCK&&c.best>=gint64(LATENCY)-5*GST_MSECOND&&c.worst<=gint64(LATENCY)+100*GST_MSECOND);
}
// Producer pause/resume mid-stream, bin vs. thin standalone composition. Both
// keep the session timeline on clock - epoch: a producer pause is a media gap
// and consumer latency is unchanged. Live audiotestsrc skips samples on resume
// without DISCONT, which framelinkaudiosink refuses as an unannounced PCM gap
// in both modes (parity, documented limitation of producer-only pause).
static std::string pauseResume(bool thinMode,const std::string&tracks){
 fprintf(stderr,"CASE pause/resume tracks=%s%s\n",tracks.c_str(),thinMode?" (thin)":" (bin)");
 const bool video=tracks!="audio",audio=tracks!="video";
 auto path=sessionPath();Consumer c;std::thread consumer([&]{c.run(path,video,audio);});g_usleep(200000);
 GstElement*p=nullptr;FlGstSession*session=nullptr;std::string text=producerText(path,tracks);
 if(thinMode){session=fl_gst_session_new((video?1u:0u)|(audio?2u:0u));flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
  if(video){caps.videoCount=1;caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};}if(audio){caps.audioCount=1;caps.audio[0]={FL_PCM_S16,48000,2};}
  GError*e=nullptr;flChannel*channel=nullptr;
  CHECK(fl_gst_session_open_local_av(session,TRUE,path.c_str(),&caps,2,0,80,5000,5000,0,&channel,nullptr,&e));
  p=parse(text);
  auto*context=fl_gst_session_create_context(session);gst_element_set_context(p,context);gst_context_unref(context);
  if(channel){auto*sb=gst_bin_get_by_name(GST_BIN(p),"s");gboolean adopted=FALSE;g_signal_emit_by_name(sb,"adopt-channel",channel,&adopted);gst_object_unref(sb);CHECK(adopted);}
  CHECK(fl_gst_session_local_ready(session,TRUE,5000,&e));CHECK(fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),TRUE,0,&e));
 }else p=producerPipeline(text,path);
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto until=g_get_monotonic_time()+10000000;
 while((video?c.frames.load()<8:c.samples<guint64(8)*BLOCK)&&g_get_monotonic_time()<until)g_usleep(5000);
 CHECK(gst_element_set_state(p,GST_STATE_PAUSED)!=GST_STATE_CHANGE_FAILURE);g_usleep(300000);
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*m=finish(p,20*GST_SECOND);CHECK(m);std::string outcome=GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR?errorText(m):"EOS";gst_message_unref(m);
 if(session&&outcome=="EOS"){GError*e=nullptr;if(!fl_gst_session_finish_local(session,TRUE,5000,&e))outcome="drain failed";g_clear_error(&e);}
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);consumer.join();if(session)g_object_unref(session);
 fprintf(stderr,"  producer=%s consumer=%s video=%u samples=%" G_GUINT64_FORMAT " lag=%.1f..%.1fms\n",outcome.c_str(),c.ok?"ok":c.error.c_str(),c.frames.load(),c.samples,c.best/1e6,c.worst/1e6);
 if(!audio){
  // Video: latency unchanged across the pause and exact END accounting (c.ok).
  // A live source discards the frame in flight when paused (GstBaseSrc).
  CHECK(outcome=="EOS"&&c.ok&&c.frames+2>=FRAMES&&c.frames<=FRAMES&&c.best>=gint64(LATENCY)-5*GST_MSECOND&&c.worst<=gint64(LATENCY)+100*GST_MSECOND);return "EOS";
 }
 // Strip the element name prefix: the refusing element is framelinkaudiosink in both modes.
 CHECK(outcome.find("PCM sample duration/continuity mismatch")!=std::string::npos);return "PCM sample duration/continuity mismatch";
}
static void teardownWhileHeld(){
 fprintf(stderr,"CASE teardown while media is held\n");
 auto*p=producerPipeline(producerText("","av"),sessionPath());CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);g_usleep(300000);
 auto begin=g_get_monotonic_time();CHECK(gst_element_set_state(p,GST_STATE_NULL)==GST_STATE_CHANGE_SUCCESS);const gint64 took=g_get_monotonic_time()-begin;
 gst_object_unref(p);fprintf(stderr,"  NULL in %.1fms\n",took/1e3);CHECK(took<1000000);
}
static void consumerDeath(){
 fprintf(stderr,"CASE consumer death mid-stream\n");
 auto path=sessionPath();Consumer c;c.abortAfter=6;std::thread consumer([&]{c.run(path,true,true);});g_usleep(200000);
 auto*p=producerPipeline(producerText(path,"av"),path);CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 consumer.join();auto begin=g_get_monotonic_time();auto*m=finish(p,10*GST_SECOND);CHECK(m);
 const bool error=GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR;std::string text=error?errorText(m):"EOS";gst_message_unref(m);const gint64 took=g_get_monotonic_time()-begin;
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);
 fprintf(stderr,"  producer error %.0fms after consumer death: %s\n",took/1e3,text.c_str());CHECK(error&&took<6000000);
}

int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#else
 CHECK(argc>1);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 // Only enabled pads exist; tracks are fixed outside NULL.
 for(auto t:{"av","video","audio"}){auto*s=gst_element_factory_make("framelinkavsink",nullptr);CHECK(s);g_object_set(s,"tracks",t,nullptr);
  auto*v=gst_element_get_static_pad(s,"video_sink"),*a=gst_element_get_static_pad(s,"audio_sink");CHECK((v!=nullptr)==(strcmp(t,"audio")!=0)&&(a!=nullptr)==(strcmp(t,"video")!=0));
  if(v)gst_object_unref(v);if(a)gst_object_unref(a);gst_object_unref(s);}
 if(const char*only=getenv("FL_AVBIN_ONLY")){if(!strcmp(only,"pause")){for(auto t:{"video","av"})CHECK(pauseResume(true,t)==pauseResume(false,t));}return 0;}
 thin();
 for(auto t:{"video","av"})CHECK(pauseResume(true,t)==pauseResume(false,t));
 teardownWhileHeld();consumerDeath();
 pair("av",true,800);pair("av",false,0);pair("video",true,300);pair("audio",false,0);
 negative("track-mismatch","av","audio",false,2000,"");
 negative("no-consumer","av","",false,1000,"");
 negative("non-system-clock","av","av",true,3000,"system clock");
 {fprintf(stderr,"CASE negative caps-mismatch\n");auto path=sessionPath();Consumer c;std::thread consumer([&]{c.run(path,true,true);});
  std::string text=producerText(path,"av");text.replace(text.find("width=64,height=32,framerate"),std::string("width=64,height=32,framerate").size(),"width=128,height=64,framerate");
  auto*p=producerPipeline(text,path);CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);auto*m=finish(p,15*GST_SECOND);CHECK(m);
  const bool error=GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR;std::string t=error?errorText(m):"EOS";gst_message_unref(m);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);consumer.join();
  fprintf(stderr,"  producer: %s; consumer: %s\n",t.c_str(),c.ok?"ok":c.error.c_str());CHECK(error&&!c.ok);}
 puts("framelinkavsink: held startup in both orders, session-epoch timeline (lag within latency bounds on both tracks), thin/bin pause parity, exact counts, drain before EOS, teardown/consumer-death/track/clock/caps/missing-peer failures bounded");
 return 0;
}
