// Two-process A/V over FrameLink with only public GStreamer elements and the
// public session helpers. Run one producer and one consumer with the same
// --session directory (private, current-user; created by the producer).
//
//   framelink_gst_av producer --session DIR [--tracks av|audio|video]
//        [--format RGBA|BGRA|NV12] [--size 640x360] [--fps 30] [--seconds 10]
//        [--video-source "videotestsrc is-live=true"] [--audio-source "audiotestsrc is-live=true"]
//   framelink_gst_av consumer --session DIR [--tracks ...] [--format ...] [--size ...] [--fps ...]
//        [--latency-ms 150] [--pool 8] [--display | --verify --seconds 10]
//
// Display mode renders with autovideosink and plays through autoaudiosink.
// Verify mode is headless: exact frame/sample counts, uniform test pixels,
// contiguous audio timestamps and delivery at PTS+latency on the system clock.
#include "framelink_session_transport.h"
#include <gst/app/gstappsink.h>
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
namespace {
struct Options {
 std::string role,session,tracks="av",format="RGBA",videoSource="videotestsrc is-live=true pattern=solid-color foreground-color=0xffff0000",audioSource="audiotestsrc is-live=true wave=sine";
 unsigned width=640,height=360,fps=30,seconds=10,latencyMs=150,pool=8,timeoutMs=15000;bool display=false,verify=false;
};
[[noreturn]] void usage(const char*why){fprintf(stderr,"%s\nsee the header of samples/gst_av_example.cpp for usage\n",why);exit(2);}
Options parse(int argc,char**argv){
 Options o;if(argc<2)usage("missing role");o.role=argv[1];if(o.role!="producer"&&o.role!="consumer")usage("role must be producer or consumer");
 for(int i=2;i<argc;i++){
  std::string a=argv[i];auto value=[&]{if(i+1>=argc)usage(("missing value for "+a).c_str());return std::string(argv[++i]);};
  if(a=="--session")o.session=value();else if(a=="--tracks")o.tracks=value();else if(a=="--format")o.format=value();
  else if(a=="--size"){auto v=value();if(sscanf(v.c_str(),"%ux%u",&o.width,&o.height)!=2)usage("--size WxH");}
  else if(a=="--fps")o.fps=unsigned(atoi(value().c_str()));else if(a=="--seconds")o.seconds=unsigned(atoi(value().c_str()));
  else if(a=="--latency-ms")o.latencyMs=unsigned(atoi(value().c_str()));else if(a=="--pool")o.pool=unsigned(atoi(value().c_str()));
  else if(a=="--timeout-ms")o.timeoutMs=unsigned(atoi(value().c_str()));
  else if(a=="--video-source")o.videoSource=value();else if(a=="--audio-source")o.audioSource=value();
  else if(a=="--display")o.display=true;else if(a=="--verify")o.verify=true;else usage(("unknown option "+a).c_str());
 }
 if(o.session.empty())usage("--session is required");
 if(o.tracks!="av"&&o.tracks!="audio"&&o.tracks!="video")usage("--tracks av|audio|video");
 if(o.format!="RGBA"&&o.format!="BGRA"&&o.format!="NV12")usage("--format RGBA|BGRA|NV12");
 if(!o.fps||!o.seconds||!o.width||!o.height||o.latencyMs>2000)usage("invalid numeric option");
 if(o.role=="consumer"&&o.display==o.verify)usage("consumer needs exactly one of --display or --verify");
 return o;
}
bool hasVideo(const Options&o){return o.tracks!="audio";}
bool hasAudio(const Options&o){return o.tracks!="video";}
flFormat format(const Options&o){return o.format=="NV12"?FL_FORMAT_NV12:o.format=="BGRA"?FL_FORMAT_BGRA8:FL_FORMAT_RGBA8;}
constexpr unsigned kRate=48000,kBlock=480; // 10ms S16LE stereo blocks
GstElement* launch(const std::string&text){GError*e=nullptr;auto*p=gst_parse_launch(text.c_str(),&e);if(e){fprintf(stderr,"pipeline: %s\n",e->message);g_clear_error(&e);if(p)gst_object_unref(p);return nullptr;}return p;}
std::string videoCaps(const Options&o){return "video/x-raw,format="+o.format+",width="+std::to_string(o.width)+",height="+std::to_string(o.height)+",framerate="+std::to_string(o.fps)+"/1";}
const char* audioCaps="audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2";

// Run the pipeline to EOS or error on the main thread; returns success.
// FrameLink diagnostics (sink stats, sequence gaps, audio stats) go to stderr.
bool runToEos(GstElement*pipeline,unsigned limitMs){
 auto*bus=gst_element_get_bus(pipeline);bool ok=false;GstMessage*m=nullptr;
 const gint64 end=g_get_monotonic_time()+gint64(limitMs)*1000;
 while(g_get_monotonic_time()<end){
  m=gst_bus_timed_pop_filtered(bus,GstClockTime(std::max<gint64>(1,end-g_get_monotonic_time()))*GST_USECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR|GST_MESSAGE_ELEMENT));
  if(!m||GST_MESSAGE_TYPE(m)!=GST_MESSAGE_ELEMENT)break;
  if(auto*st=gst_message_get_structure(m))if(g_str_has_prefix(gst_structure_get_name(st),"framelink")){gchar*t=gst_structure_to_string(st);fprintf(stderr,"%s: %s\n",GST_OBJECT_NAME(m->src),t);g_free(t);}
  gst_message_unref(m);m=nullptr;
 }
 if(!m)fprintf(stderr,"timed out waiting for EOS\n");
 else if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);fprintf(stderr,"%s: %s\n%s\n",GST_OBJECT_NAME(m->src),e->message,d?d:"");g_clear_error(&e);g_free(d);}
 else ok=true;
 if(m)gst_message_unref(m);gst_object_unref(bus);return ok;
}

struct Verify {
 std::atomic<unsigned> frames{0};std::atomic<guint64> samples{0};std::atomic<bool> bad{false};
 std::atomic<gint64> worstLag{G_MININT64},bestLag{G_MAXINT64};std::string why;
 GstClock*clock=gst_system_clock_obtain();GstClockTime nextAudio=GST_CLOCK_TIME_NONE;
 ~Verify(){gst_object_unref(clock);}
 void fail(const std::string&w){if(!bad.exchange(true))why=w;}
 void lag(GstBuffer*b){const gint64 l=gint64(gst_clock_get_time(clock))-gint64(GST_BUFFER_PTS(b));
  gint64 cur=worstLag.load();while(l>cur&&!worstLag.compare_exchange_weak(cur,l)){}
  cur=bestLag.load();while(l<cur&&!bestLag.compare_exchange_weak(cur,l)){}}
};
GstFlowReturn onVideo(GstAppSink*sink,gpointer data){
 auto*v=static_cast<Verify*>(data);auto*s=gst_app_sink_pull_sample(sink);if(!s)return GST_FLOW_ERROR;
 auto*b=gst_sample_get_buffer(s);v->lag(b);GstMapInfo m{};
 if(gst_buffer_map(b,&m,GST_MAP_READ)){
  // The default producer source is a uniform colour: every pixel (RGBA/BGRA) or
  // every Y byte / UV pair (NV12) must equal the first one.
  auto*caps=gst_sample_get_caps(s);auto*st=gst_caps_get_structure(caps,0);const char*f=gst_structure_get_string(st,"format");gint w=0,h=0;gst_structure_get_int(st,"width",&w);gst_structure_get_int(st,"height",&h);
  if(f&&!strcmp(f,"NV12")){const gsize y=gsize(w)*h;if(m.size<y+y/2)v->fail("short NV12 frame");else{for(gsize i=1;i<y;i++)if(m.data[i]!=m.data[0]){v->fail("non-uniform luma");break;}for(gsize i=y+2;i+1<y+y/2;i+=2)if(m.data[i]!=m.data[y]||m.data[i+1]!=m.data[y+1]){v->fail("non-uniform chroma");break;}}}
  else{for(gsize i=4;i+3<m.size;i+=4)if(memcmp(m.data+i,m.data,4)){v->fail("non-uniform pixels");break;}}
  gst_buffer_unmap(b,&m);
 }else v->fail("unmappable frame");
 ++v->frames;gst_sample_unref(s);return GST_FLOW_OK;
}
GstFlowReturn onAudio(GstAppSink*sink,gpointer data){
 auto*v=static_cast<Verify*>(data);auto*s=gst_app_sink_pull_sample(sink);if(!s)return GST_FLOW_ERROR;
 auto*b=gst_sample_get_buffer(s);v->lag(b);
 // Contiguous timeline: each block starts where the previous one ended (1ns rounding).
 if(GST_CLOCK_TIME_IS_VALID(v->nextAudio)){auto d=gint64(GST_BUFFER_PTS(b))-gint64(v->nextAudio);if(d<-1||d>1)v->fail("audio timestamp discontinuity");}
 v->nextAudio=GST_BUFFER_PTS(b)+GST_BUFFER_DURATION(b);v->samples+=gst_buffer_get_size(b)/4;gst_sample_unref(s);return GST_FLOW_OK;
}

int producer(const Options&o){
 auto*session=fl_gst_session_new((hasVideo(o)?1u:0u)|(hasAudio(o)?2u:0u));
 flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 if(hasVideo(o)){caps.videoCount=1;caps.video[0]={guint32(format(o)),o.width,o.height,o.fps,1};}
 if(hasAudio(o)){caps.audioCount=1;caps.audio[0]={FL_PCM_S16,kRate,2};}
 std::string text;
 if(hasVideo(o))text+=o.videoSource+" num-buffers="+std::to_string(o.fps*o.seconds)+" ! "+videoCaps(o)+" ! framelinksink name=video ";
 if(hasAudio(o))text+=o.audioSource+" num-buffers="+std::to_string(o.seconds*(kRate/kBlock))+" samplesperbuffer="+std::to_string(kBlock)+" ! "+audioCaps+" ! framelinkaudiosink tracks="+o.tracks;
 auto*pipeline=launch(text);if(!pipeline)return 1;
 fprintf(stderr,"producer: waiting for consumer on %s\n",o.session.c_str());
 GError*e=nullptr;flChannel*channel=nullptr;
 if(!fl_gst_session_open_local_av(session,TRUE,o.session.c_str(),&caps,2,0,80,o.timeoutMs,o.timeoutMs,0,&channel,nullptr,&e)){fprintf(stderr,"session setup: %s\n",e->message);return 1;}
 auto*context=fl_gst_session_create_context(session);gst_element_set_context(pipeline,context);gst_context_unref(context);
 if(channel){auto*video=gst_bin_get_by_name(GST_BIN(pipeline),"video");gboolean adopted=FALSE;g_signal_emit_by_name(video,"adopt-channel",channel,&adopted);gst_object_unref(video);if(!adopted){fprintf(stderr,"video lease rejected\n");return 1;}}
 // Do not stream until the consumer's sinks and devices exist.
 if(!fl_gst_session_local_ready(session,TRUE,o.timeoutMs,&e)){fprintf(stderr,"ready: %s\n",e->message);return 1;}
 if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(pipeline),TRUE,0,&e)){fprintf(stderr,"clock: %s\n",e->message);return 1;}
 if(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){runToEos(pipeline,1);return 1;}
 bool ok=runToEos(pipeline,o.seconds*1000+o.timeoutMs);
 // Keep the lease open until the consumer has rendered what is still queued.
 if(ok&&!fl_gst_session_finish_local(session,TRUE,o.latencyMs+o.timeoutMs,&e)){fprintf(stderr,"finish: %s\n",e->message);g_clear_error(&e);ok=false;}
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(pipeline);g_object_unref(session);
 fprintf(stderr,"producer: %s\n",ok?"finished":"failed");return ok?0:1;
}

int consumer(const Options&o){
 auto*session=fl_gst_session_new((hasVideo(o)?1u:0u)|(hasAudio(o)?2u:0u));
 flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 if(hasVideo(o)){caps.videoCount=1;caps.video[0]={guint32(format(o)),o.width,o.height,o.fps,1};}
 if(hasAudio(o)){caps.audioCount=1;caps.audio[0]={FL_PCM_S16,kRate,2};}
 // A queue after each live FrameLink source decouples it from the synchronized
 // sink; the video pool must cover frames held within the pipeline latency.
 std::string text;
 if(hasVideo(o))text+="framelinksrc name=video width="+std::to_string(o.width)+" height="+std::to_string(o.height)+" format="+o.format+" ! queue ! "+(o.display?"videoconvert ! autovideosink":"appsink name=vout sync=true emit-signals=false")+" ";
 if(hasAudio(o))text+="framelinkaudiosrc tracks="+o.tracks+" ! queue ! "+(o.display?std::string("audioconvert ! audioresample ! autoaudiosink"):std::string("appsink name=aout sync=true"));
 auto*pipeline=launch(text);if(!pipeline)return 1;
 Verify verify;
 if(o.verify){GstAppSinkCallbacks cb{};
  if(hasVideo(o)){cb.new_sample=onVideo;auto*s=gst_bin_get_by_name(GST_BIN(pipeline),"vout");gst_app_sink_set_callbacks(GST_APP_SINK(s),&cb,&verify,nullptr);gst_object_unref(s);}
  if(hasAudio(o)){cb.new_sample=onAudio;auto*s=gst_bin_get_by_name(GST_BIN(pipeline),"aout");gst_app_sink_set_callbacks(GST_APP_SINK(s),&cb,&verify,nullptr);gst_object_unref(s);}}
 fprintf(stderr,"consumer: connecting to %s\n",o.session.c_str());
 GError*e=nullptr;flChannel*channel=nullptr;
 if(!fl_gst_session_open_local_av(session,FALSE,o.session.c_str(),&caps,2,o.pool,80,o.timeoutMs,o.timeoutMs,0,&channel,nullptr,&e)){fprintf(stderr,"session setup: %s\n",e->message);return 1;}
 auto*context=fl_gst_session_create_context(session);gst_element_set_context(pipeline,context);gst_context_unref(context);
 if(channel){auto*video=gst_bin_get_by_name(GST_BIN(pipeline),"video");gboolean adopted=FALSE;g_signal_emit_by_name(video,"adopt-channel",channel,&adopted);gst_object_unref(video);if(!adopted){fprintf(stderr,"video lease rejected\n");return 1;}}
 const GstClockTime latency=GstClockTime(o.latencyMs)*GST_MSECOND;
 if(!fl_gst_session_apply_pipeline_clock(session,GST_PIPELINE(pipeline),FALSE,latency,&e)){fprintf(stderr,"clock: %s\n",e->message);return 1;}
 if(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){runToEos(pipeline,1);return 1;}
 if(!fl_gst_session_local_ready(session,FALSE,o.timeoutMs,&e)){fprintf(stderr,"ready: %s\n",e->message);return 1;}
 bool ok=runToEos(pipeline,o.seconds*1000+o.timeoutMs);
 if(ok&&!fl_gst_session_finish_local(session,FALSE,o.timeoutMs,&e)){fprintf(stderr,"finish: %s\n",e->message);g_clear_error(&e);ok=false;}
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(pipeline);g_object_unref(session);
 if(o.verify){
  const unsigned wantFrames=hasVideo(o)?o.fps*o.seconds:0;const guint64 wantSamples=hasAudio(o)?guint64(o.seconds)*kRate:0;
  if(verify.frames!=wantFrames)verify.fail("video frame count");
  if(verify.samples!=wantSamples)verify.fail("audio sample count");
  // Delivered by a synchronized sink: never before PTS+latency (5ms slack), at most 100ms after.
  if(wantFrames||wantSamples){if(verify.bestLag<gint64(latency)-5000000||verify.worstLag>gint64(latency)+100000000)verify.fail("delivery outside latency window");}
  printf("{\"event\":\"verify\",\"tracks\":\"%s\",\"format\":\"%s\",\"frames\":%u,\"expected_frames\":%u,\"samples\":%llu,\"expected_samples\":%llu,\"lag_min_ms\":%.3f,\"lag_max_ms\":%.3f,\"latency_ms\":%u,\"result\":\"%s\"%s%s%s}\n",
   o.tracks.c_str(),o.format.c_str(),verify.frames.load(),wantFrames,(unsigned long long)verify.samples.load(),(unsigned long long)wantSamples,
   verify.bestLag.load()==G_MAXINT64?0.0:verify.bestLag/1e6,verify.worstLag.load()==G_MININT64?0.0:verify.worstLag/1e6,o.latencyMs,ok&&!verify.bad?"pass":"fail",verify.bad?",\"reason\":\"":"",verify.bad?verify.why.c_str():"",verify.bad?"\"":"");
  ok=ok&&!verify.bad;
 }
 fprintf(stderr,"consumer: %s\n",ok?"finished":"failed");return ok?0:1;
}
}
int main(int argc,char**argv){
 gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#endif
 auto o=parse(argc,argv);
#ifdef __ANDROID__
 if(o.display){fprintf(stderr,"--display is not available in the Android command-line build; use --verify\n");return 2;}
#endif
 return o.role=="producer"?producer(o):consumer(o);
}
