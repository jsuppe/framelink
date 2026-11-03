// M5b task11: bin parity. Every scenario runs with the producer and consumer
// each as standalone elements (task7 application path) or as the bins
// (framelinkavsink/framelinkavsrc), in all four combinations, and the
// observable outcomes must match: counts and render lag, negotiated memory,
// early EOS, consumer pause, producer flush, peer death both ways, missing
// tracks, queue bounds, reconnect and resource cleanup.
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
 if(video){c.videoCount=1;c.video[0]={FL_FORMAT_RGBA8,64,32,30,1};}if(audio){c.audioCount=1;c.audio[0]={FL_PCM_S16,48000,2};}return c;}

// One side of a session, run on its own thread like a separate application.
struct Side {
 Mode mode;bool producer;std::string path,tracks;GstElement*p=nullptr;FlGstSession*session=nullptr;std::thread th;
 std::atomic<bool> playing{false},stop{false};std::string result;gint64 endedAt=0;
 guint audioQueueMs=80,lateMs=0;bool native=false;
 bool video()const{return tracks!="audio";}bool audio()const{return tracks!="video";}
 // Wait for EOS/error; stop() ends the wait (abrupt teardown by the test).
 std::string wait(GstClockTime limit){auto*bus=gst_element_get_bus(p);const gint64 until=g_get_monotonic_time()+gint64(limit/1000);std::string r="timeout";
  while(g_get_monotonic_time()<until){if(stop){r="stopped";break;}auto*m=gst_bus_timed_pop_filtered(bus,50*GST_MSECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
   if(!m)continue;r=GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS?"EOS":"error: "+errorText(m);gst_message_unref(m);break;}
  gst_object_unref(bus);endedAt=g_get_monotonic_time();return r;}
 void runElement(){
  session=fl_gst_session_new((video()?1u:0u)|(audio()?2u:0u));auto caps=capabilities(video(),audio());GError*e=nullptr;flChannel*channel=nullptr;
  const guint flags=native?guint(FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA):0u;
  auto fail=[&](const char*what){result=std::string("error: ")+(e?e->message:what);g_clear_error(&e);endedAt=g_get_monotonic_time();};
  if(!fl_gst_session_open_local_av(session,producer,path.c_str(),&caps,2,producer?0:8,audioQueueMs,5000,3000,producer?0:flags,&channel,nullptr,&e))return fail("setup");
  auto*context=fl_gst_session_create_context(session);gst_element_set_context(p,context);gst_context_unref(context);
  if(channel){auto*v=gst_bin_get_by_name(GST_BIN(p),"video");gboolean adopted=FALSE;g_signal_emit_by_name(v,"adopt-channel",channel,&adopted);gst_object_unref(v);if(!adopted)return fail("adopt");}
  if(producer){if(!fl_gst_session_local_ready(session,TRUE,5000,&e))return fail("ready");if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),TRUE,0,&e))return fail("clock");g_usleep(lateMs*1000);}
  else if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),FALSE,LATENCY,&e))return fail("clock");
  if(gst_element_set_state(p,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){result=wait(GST_SECOND);return;}
  if(!producer&&!fl_gst_session_local_ready(session,FALSE,5000,&e))return fail("ready");
  playing=true;result=wait(25*GST_SECOND);
  if(result=="EOS"&&!fl_gst_session_finish_local(session,producer,5000,&e))fail("drain");
 }
 void runBin(){if(gst_element_set_state(p,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){result=wait(GST_SECOND);return;}playing=true;result=wait(25*GST_SECOND);}
 void start(){th=std::thread([this]{if(mode==Mode::Bin)runBin();else runElement();});}
 void kill(){stop=true;if(th.joinable())th.join();gst_element_set_state(p,GST_STATE_NULL);}
 void finish(){if(th.joinable())th.join();gst_element_set_state(p,GST_STATE_NULL);}
 ~Side(){if(th.joinable()){stop=true;th.join();}if(p){gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);}if(session)g_object_unref(session);}
};

struct Producer:Side {
 std::atomic<guint> pushedFrames{0};std::atomic<guint64> pushedSamples{0};
 // fileLike: non-live seekable sources paced by clocksync (the coordinated
 // sinks do not pace), as for a decoded file.
 Producer(Mode m,const std::string&path_,const std::string&tracks_,guint frames,guint queueMs=80,bool fileLike=false){mode=m;producer=true;path=path_;tracks=tracks_;audioQueueMs=queueMs;
  const std::string vn=frames?" num-buffers="+std::to_string(frames):"",an=frames?" num-buffers="+std::to_string(frames*BLOCKS/FRAMES):"";
  const std::string live=fileLike?"false":"true",sync=fileLike?" ! clocksync":"";
  const std::string vs="videotestsrc name=vsrc is-live="+live+" pattern=red"+vn+" ! video/x-raw,format=RGBA,width=64,height=32,framerate=30/1"+sync+" ! ",
   as="audiotestsrc name=asrc is-live="+live+" samplesperbuffer="+std::to_string(BLOCK)+an+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2"+sync+" ! ";
  std::string t;
  if(m==Mode::Bin){t="framelinkavsink name=s tracks="+tracks+" video-format=RGBA width=64 height=32 framerate=30/1 connect-timeout-ms=5000 idle-timeout-ms=3000 audio-queue-ms="+std::to_string(queueMs)+" ";
   if(video())t+=vs+"s.video_sink ";if(audio())t+=as+"s.audio_sink";}
  else{if(video())t+=vs+"framelinksink name=video ";if(audio())t+=as+"framelinkaudiosink tracks="+tracks+" audio-queue-ms="+std::to_string(queueMs);}
  p=parse(t);if(m==Mode::Bin){auto*s=gst_bin_get_by_name(GST_BIN(p),"s");g_object_set(s,"session",path.c_str(),nullptr);gst_object_unref(s);}
  for(const char*n:{"vsrc","asrc"}){auto*e=gst_bin_get_by_name(GST_BIN(p),n);if(!e)continue;auto*pad=gst_element_get_static_pad(e,"src");
   gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*pad,GstPadProbeInfo*info,gpointer d)->GstPadProbeReturn{auto*self=static_cast<Producer*>(d);
    if(!strcmp(GST_OBJECT_NAME(GST_OBJECT_PARENT(pad)),"vsrc"))self->pushedFrames++;else self->pushedSamples+=gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info))/4;return GST_PAD_PROBE_OK;},this,nullptr);
   gst_object_unref(pad);gst_object_unref(e);}
 }
};
struct Consumer:Side {
 std::atomic<guint> frames{0};std::atomic<guint64> samples{0};gint64 trackBest[2]={G_MAXINT64,G_MAXINT64},trackWorst[2]={G_MININT64,G_MININT64};bool uniform=true,contiguous=true;
 std::string features;GstClockTime next=GST_CLOCK_TIME_NONE;std::mutex mutex;
 Consumer(Mode m,const std::string&path_,const std::string&tracks_,bool native_=false){mode=m;producer=false;path=path_;tracks=tracks_;native=native_;
  const std::string sinkV="queue ! appsink name=vout sync=true max-buffers=0 ",sinkA="queue ! appsink name=aout sync=true max-buffers=0";std::string t;
  if(m==Mode::Bin){t="framelinkavsrc name=c tracks="+tracks+" video-format=RGBA width=64 height=32 connect-timeout-ms=5000 idle-timeout-ms=3000"+(native?" native-memory=true ":" ");
   if(video())t+="c.video_src ! "+sinkV;if(audio())t+="c.audio_src ! "+sinkA;}
  else{if(video())t+=std::string("framelinksrc name=video width=64 height=32 format=RGBA")+(native?" native-memory=true":"")+" ! "+sinkV;
   if(audio())t+="framelinkaudiosrc tracks="+tracks+" ! "+sinkA;}
  p=parse(t);if(m==Mode::Bin){auto*c=gst_bin_get_by_name(GST_BIN(p),"c");g_object_set(c,"session",path.c_str(),nullptr);gst_object_unref(c);}
  for(const char*n:{"vout","aout"}){auto*sink=gst_bin_get_by_name(GST_BIN(p),n);if(!sink)continue;
   GstAppSinkCallbacks cb{};cb.new_sample=[](GstAppSink*s,gpointer d)->GstFlowReturn{auto*self=static_cast<Consumer*>(d);auto*sample=gst_app_sink_pull_sample(s);auto*b=gst_sample_get_buffer(sample);
     const int track=strcmp(GST_OBJECT_NAME(s),"vout")?1:0;std::lock_guard<std::mutex> lock(self->mutex);
     if(auto*clock=gst_element_get_clock(GST_ELEMENT(s))){gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(GST_ELEMENT(s)))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
      self->trackBest[track]=std::min(self->trackBest[track],l);self->trackWorst[track]=std::max(self->trackWorst[track],l);}
     if(track==0){if(self->features.empty()){auto*f=gst_caps_get_features(gst_sample_get_caps(sample),0);gchar*text=f?gst_caps_features_to_string(f):g_strdup("memory:SystemMemory");self->features=text;g_free(text);}
      GstMapInfo m{};if(gst_buffer_map(b,&m,GST_MAP_READ)){for(gsize i=4;i+3<m.size;i+=4)if(memcmp(m.data+i,m.data,4)){self->uniform=false;break;}gst_buffer_unmap(b,&m);}self->frames++;}
     else{if(GST_CLOCK_TIME_IS_VALID(self->next)){auto dd=gint64(GST_BUFFER_PTS(b))-gint64(self->next);if(dd<-1||dd>1)self->contiguous=false;}self->next=GST_BUFFER_PTS(b)+GST_BUFFER_DURATION(b);self->samples+=gst_buffer_get_size(b)/4;}
     gst_sample_unref(sample);return GST_FLOW_OK;};
   gst_app_sink_set_callbacks(GST_APP_SINK(sink),&cb,this,nullptr);gst_object_unref(sink);}
 }
 bool lagWithin(gint64 slackHigh=100*GST_MSECOND){for(int t=0;t<2;t++)if((t==0?video():audio())&&(trackBest[t]<gint64(LATENCY)-5*GST_MSECOND||trackWorst[t]>gint64(LATENCY)+slackHigh))return false;return true;}
};
static bool waitFor(const std::function<bool()>&f,gint64 us=10000000){auto until=g_get_monotonic_time()+us;while(!f()&&g_get_monotonic_time()<until)g_usleep(5000);return f();}

struct Outcome {std::string producer,consumer;std::string summary()const{return "producer="+producer+" consumer="+consumer;}};
static const Mode kModes[2]={Mode::Element,Mode::Bin};

static void log(const char*scenario,Mode pm,Mode cm,const Producer&p,const Consumer&c,const Outcome&o){
 fprintf(stderr,"  %-16s producer=%-7s consumer=%-7s | %s | frames %u/%u samples %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT " lag v %.1f..%.1f a %.1f..%.1fms %s\n",scenario,name(pm),name(cm),o.summary().c_str(),
  c.frames.load(),p.pushedFrames.load(),c.samples.load(),p.pushedSamples.load(),c.trackBest[0]/1e6,c.trackWorst[0]/1e6,c.trackBest[1]/1e6,c.trackWorst[1]/1e6,c.features.c_str());
}
// Startup in both orders and full streams: exact counts, lag and memory path.
static Outcome normal(Mode pm,Mode cm,const std::string&tracks,bool native,bool consumerFirst){
 auto path=sessionPath();Producer p(pm,path,tracks,FRAMES);Consumer c(cm,path,tracks,native);
 if(consumerFirst){c.start();g_usleep(200000);p.start();}else{p.start();g_usleep(200000);c.start();}
 c.finish();p.finish();Outcome o{p.result,c.result};
 const bool v=tracks!="audio",a=tracks!="video";
 log(native?"native":consumerFirst?"consumer-first":"producer-first",pm,cm,p,c,o);
 CHECK(o.producer=="EOS"&&o.consumer=="EOS");
 CHECK(c.frames==(v?FRAMES:0u)&&c.samples==(a?guint64(BLOCKS)*BLOCK:0u)&&c.contiguous&&c.lagWithin());
 if(!native)CHECK(c.uniform);
 o.consumer+=" "+c.features;return o;
}
// A clock-paced file-like producer that starts 300ms behind its timeline
// bursts frames faster than real time. The ring keeps only the newest unread
// frame, so coordinated video fails at once, naming the cause, in both modes.
// Element: the application starts PLAYING 300ms after applying the clock.
// Bin: PLAYING 300ms before the consumer exists, so media is held meanwhile.
static Outcome burst(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2,80,true);p.lateMs=300;Consumer c(cm,path,"av");
 if(pm==Mode::Bin){p.start();g_usleep(300000);c.start();}else{c.start();p.start();}
 c.finish();p.finish();Outcome o{p.result,c.result};log("burst",pm,cm,p,c,o);
 CHECK(o.consumer.find("coordinated video frame replaced before it was read")!=std::string::npos&&o.producer.rfind("error: ",0)==0&&c.frames<FRAMES*2);
 return {"error","error: coordinated video frame replaced before it was read: the producer delivered faster than real time"};
}
// The producer application ends the stream early: everything pushed arrives.
static Outcome earlyEos(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",0);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=10;}));gst_element_send_event(p.p,gst_event_new_eos());
 c.finish();p.finish();Outcome o{p.result,c.result};log("early-eos",pm,cm,p,c,o);
 CHECK(o.producer=="EOS"&&o.consumer=="EOS"&&c.frames==p.pushedFrames&&c.samples==p.pushedSamples&&c.contiguous&&c.lagWithin());return o;
}
// Consumer application pauses for 300ms mid-stream and resumes.
static Outcome consumerPause(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8;}));gst_element_set_state(c.p,GST_STATE_PAUSED);g_usleep(300000);gst_element_set_state(c.p,GST_STATE_PLAYING);
 c.finish();p.finish();Outcome o{p.result,c.result};log("consumer-pause",pm,cm,p,c,o);return o;
}
// Producer application attempts a flushing seek mid-stream. The contract
// rejects seeks; the coordinated sinks refuse it before it reaches the
// sources, so the stream stays intact.
static Outcome producerFlush(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8;}));const bool seeked=gst_element_seek_simple(p.p,GST_FORMAT_TIME,GstSeekFlags(GST_SEEK_FLAG_FLUSH),GST_SECOND/2);
 c.finish();p.finish();Outcome o{p.result,c.result};log("producer-seek",pm,cm,p,c,o);
 fprintf(stderr,"    flushing seek %s; delivered %u/%u frames, %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT " samples\n",seeked?"accepted":"refused",c.frames.load(),p.pushedFrames.load(),c.samples.load(),p.pushedSamples.load());
 o.producer+=seeked?" seek=accepted":" seek=refused";
 if(!seeked)CHECK(o.producer=="EOS seek=refused"&&o.consumer=="EOS"&&c.frames==FRAMES*2&&c.samples==guint64(BLOCKS)*BLOCK*2&&c.contiguous);
 return o;
}
// Consumer application attempts a flushing seek on its live pipeline.
static Outcome consumerSeek(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8;}));const bool seeked=gst_element_seek_simple(c.p,GST_FORMAT_TIME,GstSeekFlags(GST_SEEK_FLAG_FLUSH),0);
 c.finish();p.finish();Outcome o{p.result,c.result};log("consumer-seek",pm,cm,p,c,o);
 fprintf(stderr,"    flushing seek %s\n",seeked?"accepted":"refused");o.consumer+=seeked?" seek=accepted":" seek=refused";return o;
}
static Outcome death(Mode pm,Mode cm,bool killProducer){
 auto path=sessionPath();Producer p(pm,path,"av",0);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8&&p.playing&&c.playing;}));
 Side&dead=killProducer?static_cast<Side&>(p):static_cast<Side&>(c);Side&peer=killProducer?static_cast<Side&>(c):static_cast<Side&>(p);
 const gint64 begin=g_get_monotonic_time();dead.kill();peer.finish();const gint64 took=peer.endedAt-begin;
 Outcome o{killProducer?"killed":p.result,killProducer?c.result:"killed"};log(killProducer?"producer-death":"consumer-death",pm,cm,p,c,o);
 fprintf(stderr,"    peer reported %.0fms after death\n",took/1e3);
 CHECK(peer.result.rfind("error: ",0)==0&&took<6000000);
 // Peer-death errors are classed, not compared verbatim: which stream notices
 // first (audio socket, video ring, control connection) is a race.
 return {killProducer?"killed":"error",killProducer?"error":"killed"};
}
static Outcome mismatch(Mode pm,Mode cm,const std::string&pt,const std::string&ct){
 auto path=sessionPath();Producer p(pm,path,pt,FRAMES);Consumer c(cm,path,ct);const gint64 begin=g_get_monotonic_time();c.start();p.start();c.finish();p.finish();
 const gint64 took=std::max(p.endedAt,c.endedAt)-begin;Outcome o{p.result,c.result};log(("tracks "+pt+"/"+ct).c_str(),pm,cm,p,c,o);
 CHECK(o.producer.rfind("error: ",0)==0&&o.consumer.rfind("error: ",0)==0&&took<8000000);return o;
}
static Outcome queueBound(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES,5);Consumer c(cm,path,"av");c.start();p.start();c.finish();p.finish();
 Outcome o{p.result,c.result};log("audio-queue-5ms",pm,cm,p,c,o);return o;
}
// Two sessions on the same path, each with fresh applications/bins.
static Outcome reconnect(Mode pm,Mode cm){
 auto path=sessionPath();Outcome o;
 for(int round=0;round<2;round++){Producer p(pm,path,"av",FRAMES);Consumer c(cm,path,"av");c.start();p.start();c.finish();p.finish();
  o={p.result,c.result};log(round?"reconnect-2":"reconnect-1",pm,cm,p,c,o);CHECK(o.producer=="EOS"&&o.consumer=="EOS"&&c.frames==FRAMES&&c.samples==guint64(BLOCKS)*BLOCK);}
 return o;
}
// Bin churn: handles return to the warm-up level; a deliberate leak is caught.
static void cleanup(){
 fprintf(stderr,"CASE cleanup (bin/bin churn)\n");
 auto rounds=[](int n,bool leak){std::vector<guint> counts;std::vector<GSocket*> leaked;
  for(int i=0;i<n;i++){auto path=sessionPath();{Producer p(Mode::Bin,path,"av",10);Consumer c(Mode::Bin,path,"av");c.start();p.start();c.finish();p.finish();CHECK(p.result=="EOS"&&c.result=="EOS");}
   if(leak)for(int k=0;k<4;k++)leaked.push_back(g_socket_new(G_SOCKET_FAMILY_IPV4,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr));
   counts.push_back(handles());}
  for(auto*s:leaked)g_object_unref(s);
  const guint warm=*std::max_element(counts.begin(),counts.begin()+3);bool ok=counts.back()<=warm+4;
  std::string t;for(auto c:counts)t+=std::to_string(c)+" ";fprintf(stderr,"  %s handles: %s(warm-up max %u) -> %s\n",leak?"leak control":"churn",t.c_str(),warm,ok?"bounded":"growing");return ok;};
 CHECK(rounds(8,false));CHECK(!rounds(8,true));
}

static const char* nativeFeature(){
 auto*factory=gst_element_factory_find("framelinksrc");CHECK(factory);const char*feature=nullptr;
 for(auto*l=gst_element_factory_get_static_pad_templates(factory);l;l=l->next){auto*t=static_cast<GstStaticPadTemplate*>(l->data);if(t->direction!=GST_PAD_SRC)continue;
  auto*caps=gst_static_caps_get(&t->static_caps);
  for(guint i=0;i<gst_caps_get_size(caps);i++)for(const char*f:{"memory:DMABuf","memory:D3D11Memory","memory:FrameLinkAHardwareBuffer"})if(gst_caps_features_contains(gst_caps_get_features(caps,i),f))feature=f;
  gst_caps_unref(caps);}
 gst_object_unref(factory);return feature;
}

int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#else
 CHECK(argc>1);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 const char*only=getenv("FL_PARITY_ONLY");
 // Every scenario in the four producer/consumer combinations; outcomes must agree.
 auto parity=[&](const char*scenario,const std::function<Outcome(Mode,Mode)>&run){
  if(only&&!strstr(scenario,only))return;fprintf(stderr,"CASE %s\n",scenario);std::string first;bool same=true;
  for(Mode pm:kModes)for(Mode cm:kModes){auto o=run(pm,cm).summary();if(first.empty())first=o;else if(o!=first)same=false;}
  fprintf(stderr,"  => %s: %s\n",same?"parity":"DIFFERENT",first.c_str());CHECK(same);};
 parity("consumer-first av",[](Mode pm,Mode cm){return normal(pm,cm,"av",false,true);});
 parity("producer-first av",[](Mode pm,Mode cm){return normal(pm,cm,"av",false,false);});
 parity("burst",burst);
 parity("video-only",[](Mode pm,Mode cm){return normal(pm,cm,"video",false,true);});
 parity("audio-only",[](Mode pm,Mode cm){return normal(pm,cm,"audio",false,false);});
 // Native memory: the same negotiated platform memory on both consumer paths.
 if(const char*f=nativeFeature()){static std::string feature=f;
  parity("native-memory",[](Mode pm,Mode cm){auto o=normal(pm,cm,"av",true,true);CHECK(o.consumer=="EOS "+feature);return o;});}
 else fprintf(stderr,"CASE native-memory: not offered by this build\n");
 parity("early-eos",earlyEos);
 parity("consumer-pause",consumerPause);
 parity("producer-seek",producerFlush);
 parity("consumer-seek",consumerSeek);
 parity("producer-death",[](Mode pm,Mode cm){return death(pm,cm,true);});
 parity("consumer-death",[](Mode pm,Mode cm){return death(pm,cm,false);});
 parity("missing-audio",[](Mode pm,Mode cm){return mismatch(pm,cm,"video","av");});
 parity("missing-video",[](Mode pm,Mode cm){return mismatch(pm,cm,"av","audio");});
 parity("audio-queue-bound",queueBound);
 parity("reconnect",reconnect);
 if(!only||strstr("cleanup",only))cleanup();
 puts("bin parity: identical outcomes for element/bin producers x element/bin consumers");
 return 0;
}
