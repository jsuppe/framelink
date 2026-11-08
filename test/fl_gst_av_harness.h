// M5b shared harness: one producer or consumer application, either as the
// standalone elements (task7 application path) or as the convenience bins.
// Used by the bin parity (task11) and platform qualification (task12) tests.
#pragma once
#include "framelink_session_transport.h"
#include <gst/app/gstappsink.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#endif
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);GST_PLUGIN_STATIC_DECLARE(videotestsrc);GST_PLUGIN_STATIC_DECLARE(audiotestsrc);
}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static constexpr guint FRAMES=30,BLOCKS=50,BLOCK=480;static constexpr GstClockTime LATENCY=150*GST_MSECOND;
// Session geometry and run limit; the M5b tests use these defaults, the M6
// benchmark overrides them before building any side.
static guint gWidth=64,gHeight=32,gFps=30;static std::string gFormat="RGBA";static GstClockTime gRunLimit=25*GST_SECOND;
// Consumer video ring size and the queue in front of each appsink (the
// default queue byte limit stalls a session once frames exceed ~10 MB).
static GstClockTime gPlaybackLatency=LATENCY;static bool gOutputSync=true,gOutputAsync=true;
static bool gSharedCpu=false;
static guint gVideoPool=8,gVideoQueue=2;static std::string gQueue="queue";
static std::string geometry(){return "width="+std::to_string(gWidth)+" height="+std::to_string(gHeight);}
static flFormat sessionFormat(){return gFormat=="NV12"?FL_FORMAT_NV12:gFormat=="BGRA"?FL_FORMAT_BGRA8:FL_FORMAT_RGBA8;}
enum class Mode {Element,Bin};
static const char* name(Mode m){return m==Mode::Bin?"bin":"element";}
static GstElement* parse(const std::string&text){GError*e=nullptr;auto*p=gst_parse_launch(text.c_str(),&e);if(e)fprintf(stderr,"parse: %s\n",e->message);CHECK(p&&!e);return p;}
static std::string sessionPath(){gchar*root=g_dir_make_tmp("framelink-parity-XXXXXX",nullptr);CHECK(root);std::string p=std::string(root)+"/session";g_free(root);return p;}
static guint handles(){
#ifdef _WIN32
 DWORD n=0;GetProcessHandleCount(GetCurrentProcess(),&n);return n;
#else
 guint n=0;if(auto*d=opendir("/proc/self/fd")){while(readdir(d))n++;closedir(d);}return n;
#endif
}
// Error text without the posting element's name: inner element names differ
// between the bins and the application's pipelines.
static std::string errorText(GstMessage*m){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);std::string t=e->message;g_clear_error(&e);g_free(d);return t;}
static flSessionCapabilities capabilities(bool video,bool audio){flSessionCapabilities c{};c.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 if(video){c.videoCount=1;c.video[0]={uint32_t(sessionFormat()),gWidth,gHeight,gFps,1};}if(audio){c.audioCount=1;c.audio[0]={FL_PCM_S16,48000,2};}return c;}

// One side of a session, run on its own thread like a separate application.
struct Side {
 Mode mode;bool producer;std::string path,tracks;GstElement*p=nullptr;FlGstSession*session=nullptr;std::thread th;
 std::atomic<bool> playing{false},stop{false};std::string result;gint64 endedAt=0;
 // framelink-* element messages (EOS statistics) seen while waiting.
 std::mutex statsMutex;std::vector<GstStructure*> stats;
 guint audioQueueMs=80,lateMs=0;bool native=false;
 bool video()const{return tracks!="audio";}bool audio()const{return tracks!="video";}
 // Wait for EOS/error; stop() ends the wait (abrupt teardown by the test).
 std::string wait(GstClockTime limit){auto*bus=gst_element_get_bus(p);const gint64 until=g_get_monotonic_time()+gint64(limit/1000);std::string r="timeout";
  while(g_get_monotonic_time()<until){if(stop){r="stopped";break;}auto*m=gst_bus_timed_pop_filtered(bus,50*GST_MSECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR|GST_MESSAGE_ELEMENT));
   if(!m)continue;
   if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ELEMENT){const auto*s=gst_message_get_structure(m);if(s&&g_str_has_prefix(gst_structure_get_name(s),"framelink-")){std::lock_guard<std::mutex> lock(statsMutex);stats.push_back(gst_structure_copy(s));}gst_message_unref(m);continue;}r=GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS?"EOS":"error: "+errorText(m);gst_message_unref(m);break;}
  gst_object_unref(bus);endedAt=g_get_monotonic_time();return r;}
 void runElement(){
  session=fl_gst_session_new((video()?1u:0u)|(audio()?2u:0u));auto caps=capabilities(video(),audio());GError*e=nullptr;flChannel*channel=nullptr;
  const guint flags=native?guint(FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA):0u;
  auto fail=[&](const char*what){result=std::string("error: ")+(e?e->message:what);g_clear_error(&e);endedAt=g_get_monotonic_time();};
  if(!fl_gst_session_open_local_av(session,producer,path.c_str(),&caps,gVideoQueue,producer?0:gVideoPool,audioQueueMs,5000,3000,producer?0:flags,&channel,nullptr,&e))return fail("setup");
  auto*context=fl_gst_session_create_context(session);gst_element_set_context(p,context);gst_context_unref(context);
  if(channel){auto*v=gst_bin_get_by_name(GST_BIN(p),"video");gboolean adopted=FALSE;g_signal_emit_by_name(v,"adopt-channel",channel,&adopted);gst_object_unref(v);if(!adopted)return fail("adopt");}
  if(producer){if(!fl_gst_session_local_ready(session,TRUE,5000,&e))return fail("ready");if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),TRUE,0,&e))return fail("clock");g_usleep(lateMs*1000);}
  else if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),FALSE,gPlaybackLatency,&e))return fail("clock");
  if(gst_element_set_state(p,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){result=wait(GST_SECOND);return;}
  if(!producer&&!fl_gst_session_local_ready(session,FALSE,5000,&e))return fail("ready");
  playing=true;result=wait(gRunLimit);
  if(result=="EOS"&&!fl_gst_session_finish_local(session,producer,5000,&e))fail("drain");
 }
 void runBin(){if(gst_element_set_state(p,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){result=wait(GST_SECOND);return;}playing=true;result=wait(gRunLimit);}
 void start(){th=std::thread([this]{if(mode==Mode::Bin)runBin();else runElement();});}
 void kill(){stop=true;if(th.joinable())th.join();gst_element_set_state(p,GST_STATE_NULL);}
 void finish(){if(th.joinable())th.join();gst_element_set_state(p,GST_STATE_NULL);}
 ~Side(){if(th.joinable()){stop=true;th.join();}for(auto*s:stats)gst_structure_free(s);if(p){gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);}if(session)g_object_unref(session);}
};

struct Producer:Side {
 std::atomic<guint> pushedFrames{0};std::atomic<guint64> pushedSamples{0};
 // fileLike: non-live seekable sources paced by clocksync (the coordinated
 // sinks do not pace), as for a decoded file.
 // audioBlocks: audio buffer count; by default derived from frames (FRAMES:BLOCKS).
 Producer(Mode m,const std::string&path_,const std::string&tracks_,guint frames,guint queueMs=80,bool fileLike=false,guint audioBlocks=0){mode=m;producer=true;path=path_;tracks=tracks_;audioQueueMs=queueMs;
  const std::string vn=frames?" num-buffers="+std::to_string(frames):"",an=frames?" num-buffers="+std::to_string(audioBlocks?audioBlocks:frames*BLOCKS/FRAMES):"";
  const std::string live=fileLike?"false":"true",sync=fileLike?" ! clocksync":"";
  const std::string vs="videotestsrc name=vsrc is-live="+live+" pattern=red"+vn+" ! video/x-raw,format="+gFormat+",width="+std::to_string(gWidth)+",height="+std::to_string(gHeight)+",framerate="+std::to_string(gFps)+"/1"+sync+" ! ",
   as="audiotestsrc name=asrc is-live="+live+" samplesperbuffer="+std::to_string(BLOCK)+an+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2"+sync+" ! ";
  std::string t;
  if(m==Mode::Bin){t="framelinkavsink name=s tracks="+tracks+" video-queue-frames="+std::to_string(gVideoQueue)+" video-format="+gFormat+" "+geometry()+" framerate="+std::to_string(gFps)+"/1 connect-timeout-ms=5000 idle-timeout-ms=3000 audio-queue-ms="+std::to_string(queueMs)+" ";
   if(video())t+=vs+"s.video_sink ";if(audio())t+=as+"s.audio_sink";}
  else{if(video())t+=vs+"framelinksink name=video ";if(audio())t+=as+"framelinkaudiosink tracks="+tracks+" audio-queue-ms="+std::to_string(queueMs);}
  p=parse(t);if(m==Mode::Bin){auto*s=gst_bin_get_by_name(GST_BIN(p),"s");g_object_set(s,"session",path.c_str(),"shared-cpu",gSharedCpu,nullptr);gst_object_unref(s);}
  for(const char*n:{"vsrc","asrc"}){auto*e=gst_bin_get_by_name(GST_BIN(p),n);if(!e)continue;auto*pad=gst_element_get_static_pad(e,"src");
   gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*pad,GstPadProbeInfo*info,gpointer d)->GstPadProbeReturn{auto*self=static_cast<Producer*>(d);
    if(!strcmp(GST_OBJECT_NAME(GST_OBJECT_PARENT(pad)),"vsrc"))self->pushedFrames++;else self->pushedSamples+=gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info))/4;return GST_PAD_PROBE_OK;},this,nullptr);
   gst_object_unref(pad);gst_object_unref(e);}
 }
};
struct Consumer:Side {
 std::atomic<guint> frames{0};std::atomic<guint64> samples{0};gint64 trackBest[2]={G_MAXINT64,G_MAXINT64},trackWorst[2]={G_MININT64,G_MININT64};bool uniform=true,contiguous=true,checkContent=true;
 // record: keep every render lateness (ns) per track for distributions.
 bool record=false;std::vector<gint64> lateness[2];
 std::string features;GstClockTime next=GST_CLOCK_TIME_NONE;std::mutex mutex;
 Consumer(Mode m,const std::string&path_,const std::string&tracks_,bool native_=false,guint localSeconds=0){mode=m;producer=false;path=path_;tracks=tracks_;native=native_;
  const std::string sinkV=gQueue+" ! appsink name=vout sync="+(gOutputSync?"true":"false")+" async="+(gOutputAsync?"true":"false")+" max-buffers=0 ",sinkA=gQueue+" ! appsink name=aout sync="+(gOutputSync?"true":"false")+" async="+(gOutputAsync?"true":"false")+" max-buffers=0";std::string t;
  // Benchmark-only control: identical queues/appsinks without FrameLink or IPC.
  if(localSeconds){CHECK(m==Mode::Bin&&!native&&tracks=="av");
   t="videotestsrc is-live=true pattern=red num-buffers="+std::to_string(localSeconds*gFps)+" ! video/x-raw,format="+gFormat+",width="+std::to_string(gWidth)+",height="+std::to_string(gHeight)+",framerate="+std::to_string(gFps)+"/1 ! "+sinkV+
     " audiotestsrc is-live=true samplesperbuffer=480 num-buffers="+std::to_string(localSeconds*100)+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! "+sinkA;
  }else if(m==Mode::Bin){t="framelinkavsrc name=c tracks="+tracks+" video-queue-frames="+std::to_string(gVideoQueue)+" video-pool="+std::to_string(gVideoPool)+" video-format="+gFormat+" "+geometry()+" framerate="+std::to_string(gFps)+"/1 connect-timeout-ms=5000 idle-timeout-ms=3000"+(native?" native-memory=true ":" ");
   if(video())t+="c.video_src ! "+sinkV;if(audio())t+="c.audio_src ! "+sinkA;}
  else{if(video())t+="framelinksrc name=video "+geometry()+" format="+gFormat+(native?" native-memory=true":"")+" ! "+sinkV;
   if(audio())t+="framelinkaudiosrc tracks="+tracks+" ! "+sinkA;}
  p=parse(t);if(m==Mode::Bin&&!localSeconds){auto*c=gst_bin_get_by_name(GST_BIN(p),"c");g_object_set(c,"session",path.c_str(),"shared-cpu",gSharedCpu,"latency-ms",guint(gPlaybackLatency/GST_MSECOND),nullptr);gst_object_unref(c);}
  for(const char*n:{"vout","aout"}){auto*sink=gst_bin_get_by_name(GST_BIN(p),n);if(!sink)continue;
   GstAppSinkCallbacks cb{};cb.new_sample=[](GstAppSink*s,gpointer d)->GstFlowReturn{auto*self=static_cast<Consumer*>(d);auto*sample=gst_app_sink_pull_sample(s);auto*b=gst_sample_get_buffer(sample);
     const int track=strcmp(GST_OBJECT_NAME(s),"vout")?1:0;std::lock_guard<std::mutex> lock(self->mutex);
     if(auto*clock=gst_element_get_clock(GST_ELEMENT(s))){gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(GST_ELEMENT(s)))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
      if(self->record)self->lateness[track].push_back(l);self->trackBest[track]=std::min(self->trackBest[track],l);self->trackWorst[track]=std::max(self->trackWorst[track],l);}
     if(track==0){if(self->features.empty()){auto*f=gst_caps_get_features(gst_sample_get_caps(sample),0);gchar*text=f?gst_caps_features_to_string(f):g_strdup("memory:SystemMemory");self->features=text;g_free(text);}
      GstMapInfo m{};if(self->checkContent&&gst_buffer_map(b,&m,GST_MAP_READ)){for(gsize i=4;i+3<m.size;i+=4)if(memcmp(m.data+i,m.data,4)){self->uniform=false;break;}gst_buffer_unmap(b,&m);}self->frames++;}
     else{if(GST_CLOCK_TIME_IS_VALID(self->next)){auto dd=gint64(GST_BUFFER_PTS(b))-gint64(self->next);if(dd<-1||dd>1)self->contiguous=false;}self->next=GST_BUFFER_PTS(b)+GST_BUFFER_DURATION(b);self->samples+=gst_buffer_get_size(b)/4;}
     gst_sample_unref(sample);return GST_FLOW_OK;};
   gst_app_sink_set_callbacks(GST_APP_SINK(sink),&cb,this,nullptr);gst_object_unref(sink);}
 }
 bool lagWithin(gint64 slackHigh=100*GST_MSECOND){for(int t=0;t<2;t++)if((t==0?video():audio())&&(trackBest[t]<gint64(LATENCY)-5*GST_MSECOND||trackWorst[t]>gint64(LATENCY)+slackHigh))return false;return true;}
};
static bool waitFor(const std::function<bool()>&f,gint64 us=10000000){auto until=g_get_monotonic_time()+us;while(!f()&&g_get_monotonic_time()<until)g_usleep(5000);return f();}

// The platform native memory feature framelinksrc offers, or nullptr.
static const char* nativeFeature(){
 auto*factory=gst_element_factory_find("framelinksrc");CHECK(factory);const char*feature=nullptr;
 for(auto*l=gst_element_factory_get_static_pad_templates(factory);l;l=l->next){auto*t=static_cast<GstStaticPadTemplate*>(l->data);if(t->direction!=GST_PAD_SRC)continue;
  auto*caps=gst_static_caps_get(&t->static_caps);
  for(guint i=0;i<gst_caps_get_size(caps);i++)for(const char*f:{"memory:DMABuf","memory:D3D11Memory","memory:FrameLinkAHardwareBuffer"})if(gst_caps_features_contains(gst_caps_get_features(caps,i),f))feature=f;
  gst_caps_unref(caps);}
 gst_object_unref(factory);return feature;
}
