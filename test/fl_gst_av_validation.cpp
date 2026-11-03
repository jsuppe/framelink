// M5b task8 automated validation of the public coordinated A/V elements:
//  - deterministic scheduling on a GstTestClock (no wall-clock dependence),
//  - exact per-sample PCM and per-pixel video content across irregular packets,
//  - producer-to-consumer A/V offset, PTS-domain drift and arrival jitter
//    accounting (reported, never used to excuse an exact mismatch),
//  - fault-injection controls proving each validator check detects its fault,
//  - repeated restart/failure rounds on one coordinator pair without leaks.
#include "framelink_session_transport.h"
#include "framelink_meta.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/check/gsttestclock.h>
#include <gst/video/video.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#endif
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(coreelements);GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);
}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static constexpr guint W=30,H=17,RATE=48000;
static constexpr GstClockTime FRAME=20000000,AUDIO_LEAD_OFFSET=37000000;
// Producer segments: video start 5s, audio start 2s, both base 500ms; the
// receiver anchor 100ms maps producer running time r to consumer PTS r+100ms.
static constexpr GstClockTime VSTART=5*GST_SECOND,ASTART=2*GST_SECOND,SEGBASE=500000000,ANCHOR=100000000;
struct Pair {
 GSocketConnection *client=nullptr,*server=nullptr;
 Pair(){auto*l=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(l,nullptr,nullptr);CHECK(port);
  std::thread accept([&]{server=g_socket_listener_accept(l,nullptr,nullptr,nullptr);});auto*c=g_socket_client_new();client=g_socket_client_connect_to_host(c,"127.0.0.1",port,nullptr,nullptr);accept.join();g_object_unref(c);g_object_unref(l);CHECK(client&&server);}
 ~Pair(){g_io_stream_close(G_IO_STREAM(client),nullptr,nullptr);g_io_stream_close(G_IO_STREAM(server),nullptr,nullptr);g_object_unref(client);g_object_unref(server);}
};
static GstElement* parse(const char*text){GError*error=nullptr;auto*p=gst_parse_launch(text,&error);if(error)fprintf(stderr,"parse: %s\n",error->message);CHECK(p&&!error);return p;}
static guint8 pixel(guint x,guint y,guint c,guint n){return guint8(x*3+y*5+c*11+n*7);}
static gint16 pcm(guint64 sample,guint channel){return gint16(guint16(sample*31+channel*977+(sample>>9)));}
static GstClockTime samplesToNs(guint64 s){return gst_util_uint64_scale(s,GST_SECOND,RATE);}
static guint handles(){
#ifdef _WIN32
 DWORD n=0;GetProcessHandleCount(GetCurrentProcess(),&n);return n;
#else
 guint n=0;if(auto*d=opendir("/proc/self/fd")){while(readdir(d))n++;closedir(d);}return n;
#endif
}
static void drainErrors(GstElement*p,const char*label){auto*bus=gst_element_get_bus(p);while(auto*m=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR)){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);fprintf(stderr,"%s %s: %s (%s)\n",label,GST_OBJECT_NAME(m->src),e->message,d?d:"");g_clear_error(&e);g_free(d);gst_message_unref(m);}gst_object_unref(bus);}
static void flushBus(GstElement*p){auto*bus=gst_element_get_bus(p);while(auto*m=gst_bus_pop(bus))gst_message_unref(m);gst_object_unref(bus);}
static void pushSample(GstElement*src,GstBuffer*b,GstClockTime start){
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);segment.start=segment.position=start;segment.base=SEGBASE;
 auto*caps=gst_app_src_get_caps(GST_APP_SRC(src));auto*sample=gst_sample_new(b,caps,&segment,nullptr);
 auto flow=gst_app_src_push_sample(GST_APP_SRC(src),sample);gst_sample_unref(sample);gst_caps_unref(caps);gst_buffer_unref(b);
 if(flow!=GST_FLOW_OK)fprintf(stderr,"push %s flow=%s\n",GST_OBJECT_NAME(src),gst_flow_get_name(flow));CHECK(flow==GST_FLOW_OK);
}
static GstBuffer* videoBuffer(guint n){
 GstVideoInfo info;gst_video_info_set_format(&info,GST_VIDEO_FORMAT_RGBA,W,H);auto*b=gst_buffer_new_allocate(nullptr,GST_VIDEO_INFO_SIZE(&info),nullptr);
 GstVideoFrame f;CHECK(gst_video_frame_map(&f,&info,b,GST_MAP_WRITE));auto*d=static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&f,0));auto stride=GST_VIDEO_FRAME_PLANE_STRIDE(&f,0);
 for(guint y=0;y<H;y++)for(guint x=0;x<W;x++)for(guint c=0;c<4;c++)d[y*stride+x*4+c]=pixel(x,y,c,n);gst_video_frame_unmap(&f);
 GST_BUFFER_PTS(b)=GST_BUFFER_DTS(b)=VSTART+n*FRAME;GST_BUFFER_DURATION(b)=FRAME;return b;
}
static GstBuffer* audioBuffer(guint64 first,guint frames){
 auto*b=gst_buffer_new_allocate(nullptr,frames*4,nullptr);GstMapInfo m;CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE));auto*s=reinterpret_cast<gint16*>(m.data);
 for(guint i=0;i<frames;i++)for(guint c=0;c<2;c++)s[i*2+c]=pcm(first+i,c);gst_buffer_unmap(b,&m);
 GST_BUFFER_PTS(b)=GST_BUFFER_DTS(b)=ASTART+AUDIO_LEAD_OFFSET+samplesToNs(first);GST_BUFFER_DURATION(b)=samplesToNs(first+frames)-samplesToNs(first);return b;
}
struct Session {
 Pair audio;FlGstSession*sender=nullptr,*receiver=nullptr;GstElement*tx=nullptr,*rx=nullptr,*vin=nullptr,*ain=nullptr,*vout=nullptr,*aout=nullptr,*vs=nullptr,*vr=nullptr;
 flSessionConfig c{};flSessionCapabilities caps{};
 explicit Session(bool sync){
  sender=fl_gst_session_new(3);receiver=fl_gst_session_new(3);
  std::string sinks=sync?"sync=true":"sync=false async=false";
  rx=parse(("framelinksrc name=video width=30 height=17 format=RGBA ! queue ! appsink name=vout max-buffers=0 "+sinks+" framelinkaudiosrc name=asrc tracks=av ! queue ! appsink name=aout max-buffers=0 "+sinks).c_str());
  tx=parse("appsrc name=vin is-live=true format=time handle-segment-change=true caps=video/x-raw,format=RGBA,width=30,height=17,framerate=50/1 ! framelinksink name=video sync=false async=false "
           "appsrc name=ain is-live=true format=time handle-segment-change=true caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink name=asink tracks=av");
  vs=gst_bin_get_by_name(GST_BIN(tx),"video");vr=gst_bin_get_by_name(GST_BIN(rx),"video");vin=gst_bin_get_by_name(GST_BIN(tx),"vin");ain=gst_bin_get_by_name(GST_BIN(tx),"ain");
  vout=gst_bin_get_by_name(GST_BIN(rx),"vout");aout=gst_bin_get_by_name(GST_BIN(rx),"aout");
  auto*context=fl_gst_session_create_context(sender);gst_element_set_context(tx,context);gst_context_unref(context);
  context=fl_gst_session_create_context(receiver);gst_element_set_context(rx,context);gst_context_unref(context);
  c.version=2;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.videoTrackId=1;c.audioTrackId=2;c.video={FL_FORMAT_RGBA8,W,H,50,1};c.videoQueueFrames=2;c.audio={FL_PCM_S16,RATE,2};c.audioQueueMs=80;c.connectTimeoutMs=2000;c.idleTimeoutMs=2000;
  caps.version=2;caps.videoCount=caps.audioCount=1;caps.video[0]=c.video;caps.audio[0]=c.audio;
 }
 // Fresh generation: coordinator config, receiver mapping, authenticated audio, new video lease.
 void connect(guint64 generation,Pair&stream){
  c.generation=generation;CHECK(fl_gst_session_configure(sender,&c,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(receiver,&c,&caps,&caps)==FL_GST_SESSION_READY);
  flClockMapping mapping{};mapping.sourceClockId=c.clockId;mapping.targetClockId.bytes[0]=3;mapping.targetAnchorNs=ANCHOR;CHECK(fl_gst_session_set_mapping(receiver,generation,&mapping,0)==FL_GST_SESSION_READY);
  guint8 secret[32];memset(secret,guint8(40+generation),32);auto*key=g_bytes_new(secret,32);gboolean ok=FALSE;
  std::thread auth([&]{ok=fl_gst_session_authenticate_audio_stream(sender,stream.server,TRUE,key,1000,nullptr,nullptr);});
  CHECK(fl_gst_session_authenticate_audio_stream(receiver,stream.client,FALSE,key,1000,nullptr,nullptr));auth.join();CHECK(ok);g_bytes_unref(key);
  std::string name="m5b-validation-"+std::to_string(generation)+"-"+std::to_string(g_get_monotonic_time());flChannel*consumer=nullptr,*producer=nullptr;
  CHECK(flCreateChannelEx(name.c_str(),W,H,FL_FORMAT_RGBA8,4,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&consumer)==FL_OK);CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
  gboolean adopted=FALSE;g_signal_emit_by_name(vs,"adopt-channel",producer,&adopted);CHECK(adopted);g_signal_emit_by_name(vr,"adopt-channel",consumer,&adopted);CHECK(adopted);
 }
 void play(){
  if(gst_element_set_state(rx,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){drainErrors(rx,"rx");CHECK(!"consumer start");}
  if(gst_element_set_state(tx,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){drainErrors(tx,"tx");CHECK(!"producer start");}
 }
 void stop(){gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);flushBus(tx);flushBus(rx);}
 ~Session(){stop();for(auto*e:{vs,vr,vin,ain,vout,aout,tx,rx})gst_object_unref(e);g_object_unref(sender);g_object_unref(receiver);}
};
static GstMessage* waitEos(GstElement*p,GstClockTime timeout){auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,timeout,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));gst_object_unref(bus);return m;}
static bool isEos(GstMessage*m){bool ok=m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS;if(m&&!ok){GError*e=nullptr;gst_message_parse_error(m,&e,nullptr);fprintf(stderr,"EOS wait got error %s: %s\n",GST_OBJECT_NAME(m->src),e->message);g_clear_error(&e);}if(m)gst_message_unref(m);return ok;}

// ---- 1. Deterministic scheduling on a GstTestClock ----
// Producer pushes in its own thread; the consumer pipeline runs on a test clock
// with base time 0 and a fixed latency, so every synchronized render deadline is
// exactly mapped PTS + latency. Deadlines are released strictly in merged order
// and no sink may render before its deadline is reached.
static void testClock(){
 fprintf(stderr,"CASE testclock\n");
 constexpr guint N=10;constexpr GstClockTime L=10000000;
 Session s(true);Pair&stream=s.audio;s.connect(1,stream);
 auto*clock=gst_test_clock_new_with_start_time(0);auto*tc=GST_TEST_CLOCK(clock);
 gst_pipeline_use_clock(GST_PIPELINE(s.rx),clock);gst_element_set_start_time(s.rx,GST_CLOCK_TIME_NONE);gst_element_set_base_time(s.rx,0);gst_pipeline_set_latency(GST_PIPELINE(s.rx),L);
 // The video ring keeps the latest frames: pace each push on the source having
 // forwarded the previous one, so no frame is overwritten (no wall-clock pacing).
 std::atomic<guint> forwarded{0};auto*vpad=gst_element_get_static_pad(s.vr,"src");
 gst_pad_add_probe(vpad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*,GstPadProbeInfo*,gpointer d)->GstPadProbeReturn{(*static_cast<std::atomic<guint>*>(d))++;return GST_PAD_PROBE_OK;},&forwarded,nullptr);gst_object_unref(vpad);
 s.play();
 std::thread producer([&]{for(guint n=0;n<N;n++){pushSample(s.vin,videoBuffer(n),VSTART);auto until=g_get_monotonic_time()+2000000;while(forwarded.load()<=n&&g_get_monotonic_time()<until)g_usleep(200);CHECK(forwarded.load()>n);
   pushSample(s.ain,audioBuffer(guint64(n)*960,960),ASTART);}
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(s.vin))==GST_FLOW_OK);CHECK(gst_app_src_end_of_stream(GST_APP_SRC(s.ain))==GST_FLOW_OK);});
 struct Deadline{GstClockTime t;bool video;guint n;};std::vector<Deadline> expected;
 for(guint n=0;n<N;n++){expected.push_back({SEGBASE+ANCHOR+n*FRAME+L,true,n});expected.push_back({SEGBASE+ANCHOR+AUDIO_LEAD_OFFSET+samplesToNs(guint64(n)*960)+L,false,n});}
 std::stable_sort(expected.begin(),expected.end(),[](const Deadline&a,const Deadline&b){return a.t<b.t;});
 guint videoLeft=N,audioLeft=N;GstClockTime maxEarly=0;
 for(auto&d:expected){
  guint waiting=(videoLeft?1:0)+(audioLeft?1:0);GList*ids=nullptr;
  gst_test_clock_wait_for_multiple_pending_ids(tc,waiting,&ids);
  GstClockID earliest=nullptr;GstClockTime t=GST_CLOCK_TIME_NONE;
  for(auto*l=ids;l;l=l->next){auto id=GstClockID(l->data);auto when=gst_clock_id_get_time(id);if(when<t){t=when;earliest=id;}}
  if(t!=d.t)fprintf(stderr,"deadline %s n=%u got %" G_GUINT64_FORMAT " expected %" G_GUINT64_FORMAT "\n",d.video?"video":"audio",d.n,t,d.t);CHECK(t==d.t);
  // Neither sink may have rendered anything while its deadline is unreached.
  CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(s.vout),0)&&!gst_app_sink_try_pull_sample(GST_APP_SINK(s.aout),0));
  // process_id takes the earliest id's reference; release only the others.
  ids=g_list_remove(ids,earliest);gst_test_clock_set_time(tc,t);CHECK(gst_test_clock_process_id(tc,earliest));
  g_list_free_full(ids,(GDestroyNotify)gst_clock_id_unref);
  auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(d.video?s.vout:s.aout),2*GST_SECOND);CHECK(sample);
  auto*b=gst_sample_get_buffer(sample);auto pts=GST_BUFFER_PTS(b);CHECK(pts+L==d.t);maxEarly=std::max(maxEarly,d.t-pts);gst_sample_unref(sample);
  (d.video?videoLeft:audioLeft)--;
 }
 // Synchronized EOS waits for the last buffer's end; release it explicitly.
 // Synchronized EOS waits for the last buffer's end; advance and release it.
 producer.join();gst_test_clock_set_time(tc,expected.back().t+GST_SECOND);GstMessage*done=nullptr;
 for(auto until=g_get_monotonic_time()+3000000;!done&&g_get_monotonic_time()<until;){while(auto id=gst_test_clock_process_next_clock_id(tc))gst_clock_id_unref(id);done=waitEos(s.rx,10*GST_MSECOND);}
 CHECK(isEos(done));
 gst_object_unref(clock);
 printf("{\"case\":\"testclock\",\"deadlines\":%zu,\"latency_ns\":%" G_GUINT64_FORMAT ",\"av_offset_ns\":%" G_GUINT64_FORMAT ",\"early_renders\":0,\"result\":\"pass\"}\n",expected.size(),L,AUDIO_LEAD_OFFSET);
}

// ---- 2. Content, offset, drift and jitter with fault-injection controls ----
enum Fault{NONE,CORRUPT_PCM,CORRUPT_PIXEL,AUDIO_OFFSET,VIDEO_PTS,DROP_VIDEO,DROP_AUDIO,PRODUCER_AUDIO_GAP};
static const char* faultName(Fault f){switch(f){case NONE:return "none";case CORRUPT_PCM:return "corrupt-pcm";case CORRUPT_PIXEL:return "corrupt-pixel";case AUDIO_OFFSET:return "audio-offset-1-sample";case VIDEO_PTS:return "video-pts-1ms";case DROP_VIDEO:return "drop-video";case DROP_AUDIO:return "drop-audio-after-transport";case PRODUCER_AUDIO_GAP:return "producer-audio-gap";}return "?";}
struct Arrival{GstSample*sample;gint64 at;};
struct Probe{Fault fault;guint seen=0;};
static GstPadProbeReturn injectFault(GstPad*,GstPadProbeInfo*info,gpointer data){
 auto*p=static_cast<Probe*>(data);const guint index=p->seen++;
 if(index!=9&&p->fault!=AUDIO_OFFSET)return GST_PAD_PROBE_OK;
 if(p->fault==DROP_VIDEO||p->fault==DROP_AUDIO||p->fault==PRODUCER_AUDIO_GAP)return GST_PAD_PROBE_DROP;
 auto*b=gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));GST_PAD_PROBE_INFO_DATA(info)=b;
 if(p->fault==CORRUPT_PCM||p->fault==CORRUPT_PIXEL){GstMapInfo m;gst_buffer_map(b,&m,GST_MAP_WRITE);m.data[m.size/2]^=0x01;gst_buffer_unmap(b,&m);}
 if(p->fault==AUDIO_OFFSET){GST_BUFFER_PTS(b)+=samplesToNs(1);GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b);}
 if(p->fault==VIDEO_PTS){GST_BUFFER_PTS(b)+=1000000;GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b);}
 return GST_PAD_PROBE_OK;
}
static gint64 percentile(std::vector<gint64> v,double q){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[std::min(v.size()-1,size_t(q*(v.size()-1)+0.5))];}
static std::vector<std::string> content(Fault fault){
 fprintf(stderr,"CASE content fault=%s\n",faultName(fault));
 constexpr guint N=50;static const guint sizes[]={1,7,480,961,13,960,2399,64,1500,3};
 Session s(false);s.connect(1,s.audio);
 Probe probe{fault};
 if(fault!=NONE){// Producer-side faults enter before serialization; DROP_AUDIO loses a buffer
  // after transport so only the consumer-side validator can observe it.
  bool audio=fault==CORRUPT_PCM||fault==AUDIO_OFFSET||fault==PRODUCER_AUDIO_GAP;
  auto*target=fault==DROP_AUDIO?gst_bin_get_by_name(GST_BIN(s.rx),"asrc"):audio?gst_bin_get_by_name(GST_BIN(s.tx),"asink"):GST_ELEMENT(gst_object_ref(s.vs));
  auto*pad=gst_element_get_static_pad(target,fault==DROP_AUDIO?"src":"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,injectFault,&probe,nullptr);gst_object_unref(pad);gst_object_unref(target);}
 std::vector<Arrival> videoIn,audioIn;std::vector<gint64> videoPush;std::vector<std::pair<guint64,gint64>> audioPush; // first sample, time
 auto collect=[](GstElement*sink,std::vector<Arrival>*out){while(auto*sm=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),3*GST_SECOND))out->push_back({sm,g_get_monotonic_time()});};
 s.play();std::thread cv(collect,s.vout,&videoIn),ca(collect,s.aout,&audioIn);
 guint64 samples=0;guint chunk=0;auto begin=g_get_monotonic_time();
 for(guint n=0;n<N;n++){
  auto due=begin+gint64(n)*20000;while(g_get_monotonic_time()<due)g_usleep(500);
  videoPush.push_back(g_get_monotonic_time());pushSample(s.vin,videoBuffer(n),VSTART);
  while(samples<guint64(n+1)*960){guint frames=sizes[chunk++%G_N_ELEMENTS(sizes)];audioPush.push_back({samples,g_get_monotonic_time()});pushSample(s.ain,audioBuffer(samples,frames),ASTART);samples+=frames;}
 }
 CHECK(gst_app_src_end_of_stream(GST_APP_SRC(s.vin))==GST_FLOW_OK);CHECK(gst_app_src_end_of_stream(GST_APP_SRC(s.ain))==GST_FLOW_OK);
 bool eos=isEos(waitEos(s.rx,5*GST_SECOND));cv.join();ca.join();
 std::vector<std::string> failures;auto fail=[&](std::string why){if(failures.size()<8)fprintf(stderr,"  validator: %s\n",why.c_str());failures.push_back(std::move(why));};
 if(!eos){drainErrors(s.tx,"producer");fail("pipeline did not reach EOS");}
 // Video: count, PTS/DTS/duration, generation metadata and every pixel through the mapped stride.
 if(videoIn.size()!=N)fail("video frame count "+std::to_string(videoIn.size())+" != "+std::to_string(N));
 std::vector<gint64> videoLatency,audioLatency;GstClockTime firstVideo=GST_CLOCK_TIME_NONE,lastVideo=0;
 for(guint i=0;i<videoIn.size();i++){
  auto*b=gst_sample_get_buffer(videoIn[i].sample);auto pts=GST_BUFFER_PTS(b);
  // Identify the frame by PTS so a drop is reported once, not as N mismatches.
  guint n=pts>=SEGBASE+ANCHOR?guint((pts-SEGBASE-ANCHOR)/FRAME):0;
  if(pts!=SEGBASE+ANCHOR+n*FRAME){fail("video pts "+std::to_string(pts)+" not on the 20ms grid");continue;}
  if(n!=i)fail("video frame "+std::to_string(i)+" carries frame "+std::to_string(n));
  if(GST_BUFFER_DTS(b)!=pts||GST_BUFFER_DURATION(b)!=FRAME)fail("video dts/duration frame "+std::to_string(n));
  auto*meta=gst_buffer_get_framelink_frame_meta(b);if(!meta||meta->frame.streamGeneration!=1||meta->frame.dtsNs!=gint64(pts))fail("video metadata frame "+std::to_string(n));
  GstVideoInfo info;CHECK(gst_video_info_from_caps(&info,gst_sample_get_caps(videoIn[i].sample)));GstVideoFrame f;CHECK(gst_video_frame_map(&f,&info,b,GST_MAP_READ));
  auto*d=static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&f,0));auto stride=GST_VIDEO_FRAME_PLANE_STRIDE(&f,0);guint bad=0;
  for(guint y=0;y<H;y++)for(guint x=0;x<W;x++)for(guint c=0;c<4;c++)bad+=d[y*stride+x*4+c]!=pixel(x,y,c,n);gst_video_frame_unmap(&f);
  if(bad)fail("video pixels frame "+std::to_string(n)+": "+std::to_string(bad)+" bytes differ");
  firstVideo=std::min(firstVideo,pts);lastVideo=std::max(lastVideo,pts);
  if(n<videoPush.size())videoLatency.push_back(videoIn[i].at-videoPush[n]);
 }
 // Audio: exact concatenated PCM, per-buffer PTS from the sample index, contiguity.
 guint64 received=0;GstClockTime firstAudio=GST_CLOCK_TIME_NONE,audioEnd=0,expectedNext=SEGBASE+ANCHOR+AUDIO_LEAD_OFFSET;guint badSamples=0,discontinuities=0,ptsErrors=0;
 for(auto&a:audioIn){
  auto*b=gst_sample_get_buffer(a.sample);auto pts=GST_BUFFER_PTS(b),dur=GST_BUFFER_DURATION(b);GstMapInfo m;CHECK(gst_buffer_map(b,&m,GST_MAP_READ));
  auto*sm=reinterpret_cast<const gint16*>(m.data);guint frames=guint(m.size/4);
  if(pts!=expectedNext)discontinuities++;
  if(pts!=SEGBASE+ANCHOR+AUDIO_LEAD_OFFSET+samplesToNs(received)||dur!=samplesToNs(received+frames)-samplesToNs(received)||GST_BUFFER_DTS(b)!=pts)ptsErrors++;
  for(guint i=0;i<frames;i++)for(guint c=0;c<2;c++)badSamples+=sm[i*2+c]!=pcm(received+i,c);gst_buffer_unmap(b,&m);
  auto at=std::find_if(audioPush.begin(),audioPush.end(),[&](const std::pair<guint64,gint64>&p){return p.first==received;});if(at!=audioPush.end())audioLatency.push_back(a.at-at->second);
  firstAudio=std::min(firstAudio,pts);audioEnd=pts+dur;expectedNext=pts+dur;received+=frames;
 }
 if(received!=samples)fail("audio samples "+std::to_string(received)+" != "+std::to_string(samples));
 if(badSamples)fail("audio pcm: "+std::to_string(badSamples)+" samples differ");
 if(ptsErrors)fail("audio pts/duration: "+std::to_string(ptsErrors)+" buffers off the sample-index timeline");
 if(discontinuities)fail("audio contiguity: "+std::to_string(discontinuities)+" discontinuities");
 // Producer-to-consumer A/V offset and PTS-domain drift (exact; never tolerance-excused).
 const gint64 avOffsetError=(firstAudio==GST_CLOCK_TIME_NONE||firstVideo==GST_CLOCK_TIME_NONE)?G_MAXINT64:gint64(firstAudio)-gint64(firstVideo)-gint64(AUDIO_LEAD_OFFSET);
 const gint64 audioDrift=gint64(audioEnd-firstAudio)-gint64(samplesToNs(received));const gint64 videoDrift=gint64(lastVideo-firstVideo)-gint64(videoIn.empty()?0:(videoIn.size()-1)*FRAME);
 if(avOffsetError)fail("A/V offset error "+std::to_string(avOffsetError)+"ns");
 if(audioDrift)fail("audio drift "+std::to_string(audioDrift)+"ns");
 if(videoDrift)fail("video drift "+std::to_string(videoDrift)+"ns");
 printf("{\"case\":\"content\",\"fault\":\"%s\",\"video_frames\":%zu,\"audio_buffers\":%zu,\"audio_samples\":%" G_GUINT64_FORMAT ",\"av_offset_error_ns\":%" G_GINT64_FORMAT ",\"audio_drift_ns\":%" G_GINT64_FORMAT ",\"video_drift_ns\":%" G_GINT64_FORMAT
        ",\"arrival_us\":{\"video\":{\"p50\":%" G_GINT64_FORMAT ",\"p95\":%" G_GINT64_FORMAT ",\"max\":%" G_GINT64_FORMAT "},\"audio\":{\"p50\":%" G_GINT64_FORMAT ",\"p95\":%" G_GINT64_FORMAT ",\"max\":%" G_GINT64_FORMAT "}},\"failures\":%zu,\"result\":\"%s\"}\n",
        faultName(fault),videoIn.size(),audioIn.size(),received,avOffsetError,audioDrift,videoDrift,
        percentile(videoLatency,.5),percentile(videoLatency,.95),percentile(videoLatency,1),percentile(audioLatency,.5),percentile(audioLatency,.95),percentile(audioLatency,1),failures.size(),failures.empty()?"pass":"fail");
 fflush(stdout);
 for(auto&a:videoIn)gst_sample_unref(a.sample);for(auto&a:audioIn)gst_sample_unref(a.sample);
 return failures;
}
static void control(Fault fault,const char*expected){
 auto failures=content(fault);bool detected=false;for(auto&f:failures)detected|=f.find(expected)!=std::string::npos;
 if(!detected)fprintf(stderr,"control %s was NOT detected by '%s'\n",faultName(fault),expected);CHECK(detected);
}

// ---- 3. Restart/failure rounds on one coordinator pair ----
// Clean EOS, consumer death and producer death rotate across ten fresh
// generations on the same elements and coordinators. Every round must deliver
// exact media; failures must surface within bounds; handles must not grow.
enum Ending{CLEAN,CONSUMER_DEATH,PRODUCER_DEATH};
static bool churn(bool leakControl=false){
 fprintf(stderr,"CASE churn%s\n",leakControl?" leak-control":"");std::vector<GObject*> leaked;
 static const Ending plan[]={CLEAN,CONSUMER_DEATH,PRODUCER_DEATH,CLEAN,CONSUMER_DEATH,PRODUCER_DEATH,CLEAN,CONSUMER_DEATH,PRODUCER_DEATH,CLEAN};
 Session s(false);std::vector<guint> sampled;gint64 worstFailure=0;
 for(guint round=0;round<G_N_ELEMENTS(plan);round++){
  const guint64 generation=round+1;Pair stream;s.connect(generation,stream);s.play();
  for(guint n=0;n<4;n++){
   pushSample(s.vin,videoBuffer(n),VSTART);pushSample(s.ain,audioBuffer(guint64(n)*960,960),ASTART);
   auto*v=gst_app_sink_try_pull_sample(GST_APP_SINK(s.vout),2*GST_SECOND);auto*a=gst_app_sink_try_pull_sample(GST_APP_SINK(s.aout),2*GST_SECOND);
   if(!v||!a){drainErrors(s.tx,"tx");drainErrors(s.rx,"rx");fprintf(stderr,"round %u frame %u video=%d audio=%d\n",round,n,!!v,!!a);}
   CHECK(v&&a);auto*vb=gst_sample_get_buffer(v);auto*ab=gst_sample_get_buffer(a);
   CHECK(GST_BUFFER_PTS(vb)==SEGBASE+ANCHOR+n*FRAME&&GST_BUFFER_PTS(ab)==SEGBASE+ANCHOR+AUDIO_LEAD_OFFSET+samplesToNs(guint64(n)*960));
   auto*meta=gst_buffer_get_framelink_frame_meta(vb);CHECK(meta&&meta->frame.streamGeneration==generation);
   GstMapInfo m;CHECK(gst_buffer_map(ab,&m,GST_MAP_READ));auto*pcmData=reinterpret_cast<const gint16*>(m.data);for(guint i=0;i<960;i++)CHECK(pcmData[i*2]==pcm(guint64(n)*960+i,0));gst_buffer_unmap(ab,&m);
   gst_sample_unref(v);gst_sample_unref(a);
  }
  auto begin=g_get_monotonic_time();
  if(plan[round]==CLEAN){
   CHECK(gst_app_src_end_of_stream(GST_APP_SRC(s.ain))==GST_FLOW_OK);CHECK(gst_app_src_end_of_stream(GST_APP_SRC(s.vin))==GST_FLOW_OK);CHECK(isEos(waitEos(s.rx,3*GST_SECOND)));
  }else if(plan[round]==CONSUMER_DEATH){
   CHECK(gst_element_set_state(s.rx,GST_STATE_NULL)==GST_STATE_CHANGE_SUCCESS);CHECK(g_socket_close(g_socket_connection_get_socket(stream.client),nullptr));
   auto*bus=gst_element_get_bus(s.tx);GstMessage*err=nullptr;
   for(guint k=0;!err&&g_get_monotonic_time()-begin<3000000;k++){pushSample(s.ain,audioBuffer(guint64(4+k)*960,960),ASTART);err=gst_bus_timed_pop_filtered(bus,20*GST_MSECOND,GST_MESSAGE_ERROR);}
   gst_object_unref(bus);CHECK(err);gst_message_unref(err);flSessionConfig out{};CHECK(fl_gst_session_get_config(s.sender,&out)==FL_GST_SESSION_CANCELLED);
  }else{
   // Producer death without terminal accounting: the consumer must fail, not hang.
   CHECK(gst_element_set_state(s.tx,GST_STATE_NULL)==GST_STATE_CHANGE_SUCCESS);CHECK(g_socket_close(g_socket_connection_get_socket(stream.server),nullptr));
   auto*bus=gst_element_get_bus(s.rx);auto*err=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GST_MESSAGE_ERROR);gst_object_unref(bus);CHECK(err);gst_message_unref(err);
   auto deadline=g_get_monotonic_time()+100000;flSessionConfig out{};FlGstSessionResult state;do{state=fl_gst_session_get_config(s.receiver,&out);if(state==FL_GST_SESSION_CANCELLED)break;g_usleep(1000);}while(g_get_monotonic_time()<deadline);CHECK(state==FL_GST_SESSION_CANCELLED);
  }
  const gint64 took=g_get_monotonic_time()-begin;if(plan[round]!=CLEAN)worstFailure=std::max(worstFailure,took);
  fprintf(stderr,"round %u generation %" G_GUINT64_FORMAT " %s ended in %" G_GINT64_FORMAT "us\n",round,generation,plan[round]==CLEAN?"clean":plan[round]==CONSUMER_DEATH?"consumer-death":"producer-death",took);
  // Sampled at the same point every round (stopped, this round's sockets still open).
  // Control: four unclosed OS sockets per round, a deliberate handle leak.
  if(leakControl)for(int k=0;k<4;k++){auto*leak=g_socket_new(G_SOCKET_FAMILY_IPV4,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr);CHECK(leak);leaked.push_back(G_OBJECT(leak));}
  s.stop();sampled.push_back(handles());
 }
 // Rounds 0-2 (one of each ending) warm up lazily created threads and wakeups.
 // The final count may not exceed their maximum by more than 4: Windows'
 // process handle count moves by about +/-10 with GLib/GStreamer worker
 // threads, while a per-generation leak of 1+ handle grows by 7+ over the
 // seven later rounds (detected exactly on Linux/Android, 2+ per round on Windows).
 fprintf(stderr,"handles per round:");for(auto n:sampled)fprintf(stderr," %u",n);fprintf(stderr,"\n");
 const guint warm=*std::max_element(sampled.begin(),sampled.begin()+3);const bool grew=sampled.back()>warm+4;
 for(auto*o:leaked)g_object_unref(o);
 if(leakControl)return grew;
 CHECK(!grew);CHECK(worstFailure<3000000);
 const guint baseline=warm,final=sampled.back();
 printf("{\"case\":\"churn\",\"rounds\":%zu,\"handles_warmup_max\":%u,\"handles_final\":%u,\"worst_failure_us\":%" G_GINT64_FORMAT ",\"result\":\"pass\"}\n",G_N_ELEMENTS(plan),baseline,final,worstFailure);
 return true;
}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
#else
 CHECK(argc>1);GError*error=nullptr;auto*p=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(p&&!error);gst_object_unref(p);
#endif
 const char*only=argc>2?argv[2]:nullptr;auto want=[&](const char*n){return !only||!strcmp(only,n);};
 if(want("testclock"))testClock();
 if(want("content")){CHECK(content(NONE).empty());}
 if(want("controls")){
  control(CORRUPT_PCM,"audio pcm");control(CORRUPT_PIXEL,"video pixels");control(AUDIO_OFFSET,"A/V offset error");
  control(VIDEO_PTS,"video pts");control(DROP_VIDEO,"video frame count");control(DROP_AUDIO,"audio samples");
  // An unannounced producer PCM gap is refused before serialization and fails the session.
  control(PRODUCER_AUDIO_GAP,"pipeline did not reach EOS");
 }
 if(want("churn"))CHECK(churn());
 // The handle check must detect sockets deliberately kept alive per round.
 if(want("controls")){bool detected=churn(true);if(!detected)fprintf(stderr,"leak control NOT detected\n");CHECK(detected);}
 puts("m5b A/V validation: test-clock deadlines, exact content/offset/drift, detected fault controls and restart rounds passed");return 0;
}
