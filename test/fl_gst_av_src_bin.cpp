// framelinkavsrc (M5b task10): the consumer bin with session= against the
// validated framelinkavsink producer bin. Consumer-first and producer-first
// startup, one shared timeline (render lag = latency-ms on both tracks), exact
// content/counts, drain releasing producer EOS, native memory negotiation,
// thin composition, reconnect through NULL and bounded failures.
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
static std::string sessionPath(){gchar*root=g_dir_make_tmp("framelink-avsrc-XXXXXX",nullptr);CHECK(root);std::string p=std::string(root)+"/session";g_free(root);return p;}
static std::string errorText(GstMessage*m){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);std::string t=std::string(GST_OBJECT_NAME(m->src))+": "+e->message;g_clear_error(&e);g_free(d);return t;}
static GstMessage* finish(GstElement*p,GstClockTime timeout){auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,timeout,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));gst_object_unref(bus);return m;}
static std::string outcome(GstElement*p,GstClockTime timeout,const char*what){auto*m=finish(p,timeout);std::string t=!m?std::string(what)+" timeout":GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR?errorText(m):"EOS";if(m)gst_message_unref(m);return t;}
static void setSession(GstElement*p,const char*name,const std::string&path){auto*e=gst_bin_get_by_name(GST_BIN(p),name);CHECK(e);g_object_set(e,"session",path.c_str(),nullptr);gst_object_unref(e);}

static std::string producerText(const std::string&tracks,const std::string&extra=""){
 std::string t="framelinkavsink name=s tracks="+tracks+" video-format=RGBA width=64 height=32 framerate=30/1 connect-timeout-ms=5000 "+extra+" ";
 if(tracks!="audio")t+="videotestsrc is-live=true num-buffers="+std::to_string(FRAMES)+" pattern=red ! video/x-raw,format=RGBA,width=64,height=32,framerate=30/1 ! s.video_sink ";
 if(tracks!="video")t+="audiotestsrc is-live=true num-buffers="+std::to_string(BLOCKS)+" samplesperbuffer="+std::to_string(BLOCK)+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! s.audio_sink";
 return t;
}
static GstElement* producer(const std::string&path,const std::string&tracks,const std::string&extra=""){auto*p=parse(producerText(tracks,extra));setSession(p,"s",path);return p;}

// What the application's sinks observe on each consumer pad.
struct Rx {
 std::atomic<guint> frames{0};guint64 samples=0;gint64 best=G_MAXINT64,worst=G_MININT64,trackBest[2]={G_MAXINT64,G_MAXINT64},trackWorst[2]={G_MININT64,G_MININT64};
 bool uniform=true,contiguous=true;guint mapped=0;std::string features;GstClockTime next=GST_CLOCK_TIME_NONE;
 void reset(){frames=0;samples=0;best=trackBest[0]=trackBest[1]=G_MAXINT64;worst=trackWorst[0]=trackWorst[1]=G_MININT64;uniform=contiguous=true;mapped=0;features.clear();next=GST_CLOCK_TIME_NONE;}
 void lag(GstElement*sink,GstBuffer*b,int track){auto*clock=gst_element_get_clock(sink);if(!clock)return;
  gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(sink))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
  best=std::min(best,l);worst=std::max(worst,l);trackBest[track]=std::min(trackBest[track],l);trackWorst[track]=std::max(trackWorst[track],l);}
};
static std::string consumerText(const std::string&tracks,const std::string&extra=""){
 std::string t="framelinkavsrc name=c tracks="+tracks+" video-format=RGBA width=64 height=32 connect-timeout-ms=5000 idle-timeout-ms=3000 "+extra+" ";
 if(tracks!="audio")t+="c.video_src ! queue ! appsink name=vout sync=true max-buffers=0 ";
 if(tracks!="video")t+="c.audio_src ! queue ! appsink name=aout sync=true max-buffers=0";
 return t;
}
static GstElement* consumer(const std::string&path,const std::string&tracks,Rx&rx,const std::string&extra=""){
 auto*p=parse(consumerText(tracks,extra));if(!path.empty())setSession(p,"c",path);
 for(const char*name:{"vout","aout"}){auto*sink=gst_bin_get_by_name(GST_BIN(p),name);if(!sink)continue;
  GstAppSinkCallbacks cb{};cb.new_sample=[](GstAppSink*s,gpointer d)->GstFlowReturn{auto*rx=static_cast<Rx*>(d);auto*sample=gst_app_sink_pull_sample(s);auto*b=gst_sample_get_buffer(sample);
    const bool video=!strcmp(GST_OBJECT_NAME(s),"vout");rx->lag(GST_ELEMENT(s),b,video?0:1);
    if(video){
     if(rx->features.empty()){auto*f=gst_caps_get_features(gst_sample_get_caps(sample),0);gchar*text=f?gst_caps_features_to_string(f):g_strdup("");rx->features=text;g_free(text);}
     GstMapInfo m{};if(gst_buffer_map(b,&m,GST_MAP_READ)){rx->mapped++;for(gsize i=4;i+3<m.size;i+=4)if(memcmp(m.data+i,m.data,4)){rx->uniform=false;break;}gst_buffer_unmap(b,&m);}
     rx->frames++;
    }else{if(GST_CLOCK_TIME_IS_VALID(rx->next)){auto d=gint64(GST_BUFFER_PTS(b))-gint64(rx->next);if(d<-1||d>1)rx->contiguous=false;}rx->next=GST_BUFFER_PTS(b)+GST_BUFFER_DURATION(b);rx->samples+=gst_buffer_get_size(b)/4;}
    gst_sample_unref(sample);return GST_FLOW_OK;};
  gst_app_sink_set_callbacks(GST_APP_SINK(sink),&cb,&rx,nullptr);gst_object_unref(sink);}
 return p;
}
// An element offering a non-system clock, as an audio device would. Pipeline
// auto-selection would pick it; both bins must install the system clock.
typedef struct {GstElement parent;GstClock*clock;} ClockProvider;typedef struct {GstElementClass parent;} ClockProviderClass;
G_DEFINE_TYPE(ClockProvider,clock_provider,GST_TYPE_ELEMENT)
static GstClock* provideClock(GstElement*e){return GST_CLOCK(gst_object_ref(reinterpret_cast<ClockProvider*>(e)->clock));}
static void clockDispose(GObject*o){gst_clear_object(&reinterpret_cast<ClockProvider*>(o)->clock);G_OBJECT_CLASS(clock_provider_parent_class)->dispose(o);}
static void clock_provider_init(ClockProvider*c){c->clock=gst_test_clock_new();GST_OBJECT_FLAG_SET(c,GST_ELEMENT_FLAG_PROVIDE_CLOCK);}
static void clock_provider_class_init(ClockProviderClass*k){GST_ELEMENT_CLASS(k)->provide_clock=provideClock;G_OBJECT_CLASS(k)->dispose=clockDispose;}
static void addClockProvider(GstElement*p){auto*e=GST_ELEMENT(g_object_new(clock_provider_get_type(),nullptr));CHECK(gst_bin_add(GST_BIN(p),e));}
struct Identity {guint64 generation=0;guint video=0,audio=0;std::string id;};
static Identity identity(GstElement*p){auto*c=gst_bin_get_by_name(GST_BIN(p),"c");Identity r;gchar*id=nullptr;
 g_object_get(c,"generation",&r.generation,"video-track-id",&r.video,"audio-track-id",&r.audio,"session-id",&id,nullptr);r.id=id?id:"";g_free(id);gst_object_unref(c);return r;}
static void checkReceived(const Rx&rx,bool video,bool audio){
 CHECK(rx.frames==(video?FRAMES:0u)&&rx.samples==(audio?guint64(BLOCKS)*BLOCK:0u)&&rx.uniform&&rx.contiguous);
 // Mapped PTS are receiver system-clock capture times: with base time 0 every
 // buffer renders latency-ms after capture, on each enabled track.
 for(int t=0;t<2;t++)if(t==0?video:audio)CHECK(rx.trackBest[t]>=gint64(LATENCY)-5*GST_MSECOND&&rx.trackWorst[t]<=gint64(LATENCY)+100*GST_MSECOND);
}
static void report(const char*label,const std::string&pr,const std::string&co,const Rx&rx){
 fprintf(stderr,"  %s producer=%s consumer=%s video=%u (mapped %u, %s) samples=%" G_GUINT64_FORMAT " lag video %.1f..%.1fms audio %.1f..%.1fms\n",label,pr.c_str(),co.c_str(),rx.frames.load(),rx.mapped,
  rx.features.empty()?"-":rx.features.c_str(),rx.samples,rx.trackBest[0]/1e6,rx.trackWorst[0]/1e6,rx.trackBest[1]/1e6,rx.trackWorst[1]/1e6);
}

static void pair(const std::string&tracks,bool consumerFirst,guint leadMs,const std::string&consumerExtra="",const char*nativeFeature=nullptr,bool clockProvider=false){
 fprintf(stderr,"CASE tracks=%s consumer-first=%d lead=%ums %s%s\n",tracks.c_str(),consumerFirst,leadMs,consumerExtra.c_str(),clockProvider?" (clock-providing element in both pipelines)":"");
 const bool video=tracks!="audio",audio=tracks!="video";auto path=sessionPath();Rx rx;
 auto*c=consumer(path,tracks,rx,consumerExtra);auto*p=producer(path,tracks);if(clockProvider){addClockProvider(c);addClockProvider(p);}
 GstElement*first=consumerFirst?c:p,*second=consumerFirst?p:c;
 CHECK(gst_element_set_state(first,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);g_usleep(leadMs*1000);
 CHECK(gst_element_set_state(second,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 // The bin installed the consumer timeline on its pipeline.
 {auto*clock=gst_pipeline_get_clock(GST_PIPELINE(c));CHECK(clock&&GST_IS_SYSTEM_CLOCK(clock));gst_object_unref(clock);
  CHECK(gst_pipeline_get_latency(GST_PIPELINE(c))==LATENCY&&gst_element_get_start_time(c)==GST_CLOCK_TIME_NONE&&gst_element_get_base_time(c)==0);
  clock=gst_pipeline_get_clock(GST_PIPELINE(p));CHECK(clock&&GST_IS_SYSTEM_CLOCK(clock));gst_object_unref(clock);}
 const std::string co=outcome(c,25*GST_SECOND,"consumer EOS"),pr=outcome(p,25*GST_SECOND,"producer EOS");
 auto id=identity(c);
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);gst_element_set_state(c,GST_STATE_NULL);gst_object_unref(c);
 report("",pr,co,rx);
 CHECK(pr=="EOS"&&co=="EOS");checkReceived(rx,video,audio);
 CHECK(id.generation==1&&id.id.size()==32&&(id.video!=0)==video&&(id.audio!=0)==audio);
 if(nativeFeature)CHECK(rx.features==nativeFeature);else if(video)CHECK(rx.features.empty()||rx.features=="memory:SystemMemory");
}

// Thin composition: the application owns consumer setup, clock and adoption.
static void thin(){
 fprintf(stderr,"CASE thin (application-installed context)\n");
 auto path=sessionPath();Rx rx;auto*p=producer(path,"av");CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*session=fl_gst_session_new(3);flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;caps.videoCount=caps.audioCount=1;
 caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};caps.audio[0]={FL_PCM_S16,48000,2};GError*e=nullptr;flChannel*channel=nullptr;
 CHECK(fl_gst_session_open_local_av(session,FALSE,path.c_str(),&caps,2,8,80,5000,5000,0,&channel,nullptr,&e));
 auto*c=consumer("","av",rx);auto*context=fl_gst_session_create_context(session);gst_element_set_context(c,context);gst_context_unref(context);
 auto*bin=gst_bin_get_by_name(GST_BIN(c),"c");gboolean adopted=FALSE;g_signal_emit_by_name(bin,"adopt-channel",channel,&adopted);gst_object_unref(bin);CHECK(adopted);
 CHECK(fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(c),FALSE,LATENCY,&e));
 CHECK(gst_element_set_state(c,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(fl_gst_session_local_ready(session,FALSE,5000,&e));
 const std::string co=outcome(c,20*GST_SECOND,"consumer EOS");CHECK(co=="EOS");CHECK(fl_gst_session_finish_local(session,FALSE,5000,&e));
 const std::string pr=outcome(p,20*GST_SECOND,"producer EOS");
 gst_element_set_state(c,GST_STATE_NULL);gst_object_unref(c);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);g_object_unref(session);
 report("",pr,co,rx);CHECK(pr=="EOS");checkReceived(rx,true,true);
}

// Standalone producer (task7 path: individual elements, application setup)
// against the consumer bin.
static void standaloneProducer(){
 fprintf(stderr,"CASE standalone-element producer\n");
 auto path=sessionPath();Rx rx;auto*c=consumer(path,"av",rx);CHECK(gst_element_set_state(c,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*session=fl_gst_session_new(3);flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;caps.videoCount=caps.audioCount=1;
 caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};caps.audio[0]={FL_PCM_S16,48000,2};GError*e=nullptr;flChannel*channel=nullptr;
 CHECK(fl_gst_session_open_local_av(session,TRUE,path.c_str(),&caps,2,0,80,5000,5000,0,&channel,nullptr,&e));
 auto*p=parse("videotestsrc is-live=true num-buffers="+std::to_string(FRAMES)+" pattern=red ! video/x-raw,format=RGBA,width=64,height=32,framerate=30/1 ! framelinksink name=video "
  "audiotestsrc is-live=true num-buffers="+std::to_string(BLOCKS)+" samplesperbuffer="+std::to_string(BLOCK)+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! framelinkaudiosink tracks=av");
 auto*context=fl_gst_session_create_context(session);gst_element_set_context(p,context);gst_context_unref(context);
 auto*v=gst_bin_get_by_name(GST_BIN(p),"video");gboolean adopted=FALSE;g_signal_emit_by_name(v,"adopt-channel",channel,&adopted);gst_object_unref(v);CHECK(adopted);
 CHECK(fl_gst_session_local_ready(session,TRUE,5000,&e));CHECK(fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(p),TRUE,0,&e));
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 const std::string pr=outcome(p,20*GST_SECOND,"producer EOS");CHECK(pr=="EOS");
 // The bin's drain report completes the producer's bounded finish.
 CHECK(fl_gst_session_finish_local(session,TRUE,5000,&e));
 const std::string co=outcome(c,20*GST_SECOND,"consumer EOS");
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);gst_element_set_state(c,GST_STATE_NULL);gst_object_unref(c);g_object_unref(session);
 report("",pr,co,rx);CHECK(co=="EOS");checkReceived(rx,true,true);
}

// Reconnect: one consumer pipeline, a fresh session per NULL cycle.
static void reconnect(){
 fprintf(stderr,"CASE reconnect through NULL\n");
 auto path=sessionPath();Rx rx;auto*c=consumer(path,"av",rx);std::string ids[2];
 for(int round=0;round<2;round++){
  rx.reset();auto*p=producer(path,"av");
  CHECK(gst_element_set_state(c,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  const std::string co=outcome(c,25*GST_SECOND,"consumer EOS"),pr=outcome(p,25*GST_SECOND,"producer EOS");ids[round]=identity(c).id;
  gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);gst_element_set_state(c,GST_STATE_NULL);
  report(round?"round 1":"round 0",pr,co,rx);CHECK(pr=="EOS"&&co=="EOS");checkReceived(rx,true,true);
 }
 gst_object_unref(c);fprintf(stderr,"  sessions %s / %s\n",ids[0].c_str(),ids[1].c_str());CHECK(ids[0].size()==32&&ids[1].size()==32&&ids[0]!=ids[1]);
}

// Producer death mid-stream: the consumer bin reports an error, bounded.
static void producerDeath(){
 fprintf(stderr,"CASE producer death mid-stream\n");
 auto path=sessionPath();Rx rx;auto*c=consumer(path,"av",rx);
 auto*p=parse("framelinkavsink name=s tracks=av video-format=RGBA width=64 height=32 framerate=30/1 connect-timeout-ms=5000 "
  "videotestsrc is-live=true pattern=red ! video/x-raw,format=RGBA,width=64,height=32,framerate=30/1 ! s.video_sink "
  "audiotestsrc is-live=true samplesperbuffer=480 ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! s.audio_sink");setSession(p,"s",path);
 CHECK(gst_element_set_state(c,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto until=g_get_monotonic_time()+10000000;while(rx.frames.load()<8&&g_get_monotonic_time()<until)g_usleep(5000);CHECK(rx.frames.load()>=8);
 auto begin=g_get_monotonic_time();gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);
 const std::string co=outcome(c,15*GST_SECOND,"consumer error");const gint64 took=g_get_monotonic_time()-begin;
 gst_element_set_state(c,GST_STATE_NULL);gst_object_unref(c);
 fprintf(stderr,"  consumer %.0fms after producer death: %s\n",took/1e3,co.c_str());CHECK(co!="EOS"&&co.find("timeout")==std::string::npos&&took<6000000);
}

static void negative(const char*name,const std::string&tracks,const std::string&producerTracks,const std::string&extra,bool testClock,guint connectMs,const char*expected){
 fprintf(stderr,"CASE negative %s\n",name);
 auto path=sessionPath();Rx rx;auto*c=consumer(path,tracks,rx,extra+" connect-timeout-ms="+std::to_string(connectMs));
 GstClock*clock=nullptr;if(testClock){clock=gst_test_clock_new();gst_pipeline_use_clock(GST_PIPELINE(c),clock);}
 GstElement*p=producerTracks.empty()?nullptr:producer(path,producerTracks);if(p)CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 // A refused clock fails the state change itself; other failures arrive later.
 auto begin=g_get_monotonic_time();const bool started=gst_element_set_state(c,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE;CHECK(started||testClock);
 const std::string co=outcome(c,GstClockTime(3ull*connectMs+5000)*GST_MSECOND,"consumer");const gint64 took=g_get_monotonic_time()-begin;
 gst_element_set_state(c,GST_STATE_NULL);gst_object_unref(c);if(clock)gst_object_unref(clock);
 std::string pr="-";if(p){pr=outcome(p,10*GST_SECOND,"producer");gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);}
 fprintf(stderr,"  consumer error after %.0fms: %s; producer: %s\n",took/1e3,co.c_str(),pr.c_str());
 CHECK(co!="EOS"&&co.find("timeout")==std::string::npos&&co.find(expected)!=std::string::npos&&took<gint64(2ull*connectMs+3000)*1000&&pr!="EOS");
}
static void teardownDuringSetup(){
 fprintf(stderr,"CASE teardown during setup\n");
 Rx rx;auto*c=consumer(sessionPath(),"av",rx);CHECK(gst_element_set_state(c,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);g_usleep(300000);
 auto begin=g_get_monotonic_time();CHECK(gst_element_set_state(c,GST_STATE_NULL)==GST_STATE_CHANGE_SUCCESS);const gint64 took=g_get_monotonic_time()-begin;
 gst_object_unref(c);fprintf(stderr,"  NULL in %.1fms\n",took/1e3);CHECK(took<1000000);
}
static const char* nativeFeature(){
 // The platform feature framelinksrc offers in this build, if any.
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
 // Only enabled pads exist; no display or audio device is inside the bin.
 for(auto t:{"av","video","audio"}){auto*s=gst_element_factory_make("framelinkavsrc",nullptr);CHECK(s);g_object_set(s,"tracks",t,nullptr);
  auto*v=gst_element_get_static_pad(s,"video_src"),*a=gst_element_get_static_pad(s,"audio_src");CHECK((v!=nullptr)==(strcmp(t,"audio")!=0)&&(a!=nullptr)==(strcmp(t,"video")!=0));
  if(v)gst_object_unref(v);if(a)gst_object_unref(a);
  CHECK(guint(GST_BIN(s)->numchildren)==(strcmp(t,"av")?1u:2u));gst_object_unref(s);}
 {fprintf(stderr,"CASE negative outside a pipeline\n");auto*s=gst_element_factory_make("framelinkavsrc",nullptr);g_object_set(s,"session",sessionPath().c_str(),nullptr);
  CHECK(gst_element_set_state(s,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);gst_element_set_state(s,GST_STATE_NULL);gst_object_unref(s);}
 if(const char*only=getenv("FL_AVSRC_ONLY")){const char*f=nativeFeature();if(!strcmp(only,"native")&&f)pair("av",true,0,"native-memory=true",f);
  else if(!strcmp(only,"pair"))pair("av",true,0);else if(!strcmp(only,"clock"))pair("av",true,0,"",nullptr,true);return 0;}
 pair("av",true,0);pair("av",false,800);pair("video",false,300);pair("audio",true,0);
 pair("av",true,0,"",nullptr,true);
 if(const char*f=nativeFeature())pair("av",true,0,"native-memory=true",f);else fprintf(stderr,"CASE native memory: not offered by this build\n");
 thin();standaloneProducer();reconnect();producerDeath();teardownDuringSetup();
 negative("no-producer","av","","",false,1000,"");
 negative("track-mismatch","av","audio","",false,2000,"");
 negative("caps-mismatch","av","av","width=128 height=64",false,2000,"");
 negative("non-system-clock","av","","",true,3000,"system clock");
 puts("framelinkavsrc: consumer/producer-first startup, consumer timeline (lag within latency bounds per track), exact counts/content, drain releases producer EOS, system clock over a clock-providing element, native memory negotiation, thin composition, standalone-element producer, reconnect via NULL, producer-death/track/caps/clock/missing-peer/outside-pipeline failures bounded");
 return 0;
}
