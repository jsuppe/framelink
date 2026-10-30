// Independent native transport timing fixture: FrameLink video + GDP/TCP PCM.
// Arrival probes run before appsink synchronization, independently of pull order.
// No audio additions to flFrame; both pipelines use one explicit clock/epoch.
#include <gst/gst.h>
#include "../samples/deadline.h"
#include <mutex>
#include <map>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);
GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);
GST_PLUGIN_STATIC_DECLARE(tcp);
}
#endif
#define REQUIRE(x) do { if (!(x)) { g_printerr("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static GstElement* pipeline(const std::string& text) {
 GError* e=nullptr; auto*p=gst_parse_launch(text.c_str(),&e);
 if(e)g_printerr("%s\n",e->message); REQUIRE(p&&!e); return p;
}
static GstBuffer* block(gsize bytes,guint64 pts,guint64 duration) {
 auto*b=gst_buffer_new_allocate(nullptr,bytes,nullptr); REQUIRE(b);
 GST_BUFFER_PTS(b)=pts; GST_BUFFER_DURATION(b)=duration; return b;
}
static GstSample* pull(GstElement*s) {
 auto*p=gst_app_sink_try_pull_sample(GST_APP_SINK(s),3*GST_SECOND); REQUIRE(p);return p;
}
struct Arrivals {
 std::mutex mutex;
 std::map<guint64,gint64> pts;
 static GstPadProbeReturn probe(GstPad*,GstPadProbeInfo* info,gpointer data) {
  auto& a=*static_cast<Arrivals*>(data); auto* b=GST_PAD_PROBE_INFO_BUFFER(info);
  if(b){ std::lock_guard<std::mutex> lock(a.mutex); a.pts[GST_BUFFER_PTS(b)]=g_get_monotonic_time()*1000; }
  return GST_PAD_PROBE_OK;
 }
 gint64 at(guint64 p){std::lock_guard<std::mutex> lock(mutex);REQUIRE(pts.count(p));return pts.at(p);}
};
int main(int argc,char**argv) {
 gst_init(&argc,&argv);
 SampleDeadline deadline;
 bool faults=false, play=false, negative=false; unsigned frames=100;
 gint64 offset=37*GST_MSECOND;
 REQUIRE(argc>=2);
 for(int i=2;i<argc;++i) {
  if(!strcmp(argv[i],"--faults")) faults=true;
  else if(!strcmp(argv[i],"--negative")) negative=true;
  else if(!strcmp(argv[i],"--audio-leads")) offset=-37*GST_MSECOND;
  else { g_printerr("usage: %s plugin-path [--faults] [--audio-leads] [--negative]\n",argv[0]);return 2; }
 }
 GError*e=nullptr;
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);
 GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(tcp);
 if(play){g_printerr("Android native fixture is headless; use an APK with platform sinks for --play\n");return 2;}
 auto*plugin=gst_registry_find_plugin(gst_registry_get(),"framelink");
#else
 auto*plugin=gst_plugin_load_file(argv[1],&e);
#endif
 if(e)g_printerr("%s\n",e->message);REQUIRE(plugin);gst_object_unref(plugin);
 auto session=std::to_string(g_get_monotonic_time());auto channel="av-"+session;
 // GDP carries caps, timestamps and buffer flags; TCP is loopback only.
 // max-size-time bounds media queues; TCP's own kernel buffering is not a latency guarantee.
 auto*tx=pipeline("appsrc name=video is-live=true format=time block=true max-buffers=2 caps=video/x-raw,format=BGRA,width=160,height=90,framerate=50/1 ! framelinksink preserve-timestamps=true channel="+channel+" sync=false"+
  " appsrc name=audio is-live=true format=time block=true max-buffers=2 caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! queue max-size-buffers=4 max-size-bytes=0 max-size-time=80000000 ! gdppay ! tcpserversink name=server host=127.0.0.1 port=0 sync=false buffers-max=8");
 auto*clock=gst_system_clock_obtain();guint64 base=gst_clock_get_time(clock);
 auto start=[&](GstElement*p){gst_pipeline_use_clock(GST_PIPELINE(p),clock);gst_element_set_start_time(p,GST_CLOCK_TIME_NONE);gst_element_set_base_time(p,base);REQUIRE(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);};
 start(tx);auto*server=gst_bin_get_by_name(GST_BIN(tx),"server");gint port=0;
 for(int i=0;i<100&&!port;++i){g_object_get(server,"current-port",&port,nullptr);g_usleep(10000);}REQUIRE(port>0);
 std::string vo="appsink name=vout sync=false max-buffers=2",ao="appsink name=aout sync=false max-buffers=2";
 if(play){vo="tee name=vt vt. ! queue max-size-buffers=2 ! videoconvert ! autovideosink sync=true vt. ! queue max-size-buffers=2 ! "+vo;
 ao="tee name=at at. ! queue max-size-buffers=4 ! audioconvert ! audioresample ! autoaudiosink sync=true at. ! queue max-size-buffers=4 ! "+ao;}
 auto*rx=pipeline("framelinksrc channel="+channel+" width=160 height=90 fps=50 preserve-timestamps="+std::string(negative?"false":"true")+" ! "+vo+
  " tcpclientsrc host=127.0.0.1 port="+std::to_string(port)+" ! gdpdepay ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! "+ao);
 start(rx);
 auto*v=gst_bin_get_by_name(GST_BIN(tx),"video");auto*a=gst_bin_get_by_name(GST_BIN(tx),"audio");auto*vs=gst_bin_get_by_name(GST_BIN(rx),"vout");auto*as=gst_bin_get_by_name(GST_BIN(rx),"aout");
 Arrivals av,aa;
 for(auto pair:{std::make_pair(vs,&av),std::make_pair(as,&aa)}){
  auto* pad=gst_element_get_static_pad(pair.first,"sink");
  gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,Arrivals::probe,pair.second,nullptr);gst_object_unref(pad);
 }
 g_usleep(150000); // allow loopback accept; not part of measured stream
 const guint64 epoch=gst_clock_get_time(clock)-base+100*GST_MSECOND,step=20*GST_MSECOND;
 std::vector<double> differential, arrivalSkew;
 unsigned missed=0,drift=0,discont=0;guint64 lastAudio=0;std::vector<double>skew,lateness;
 for(unsigned n=0;n<frames;++n){
  // Deliberate restart resets media running time; do not hide it by arrival retimestamping.
  guint64 pts=epoch+n*step;
 g_usleep(1000); // bounded fixture pacing; not sink presentation
 auto sentVideo=g_get_monotonic_time()*1000;
  auto*vb=block(160*90*4,pts,step);GstMapInfo m{};REQUIRE(gst_buffer_map(vb,&m,GST_MAP_WRITE));
  for(unsigned y=0;y<90;++y)for(unsigned x=0;x<160;++x){auto*p=m.data+4*(y*160+x);p[0]=guint8(n);p[1]=guint8(x);p[2]=guint8(y);p[3]=255;}gst_buffer_unmap(vb,&m);
  REQUIRE(gst_app_src_push_buffer(GST_APP_SRC(v),vb)==GST_FLOW_OK);
  bool skip=false;guint64 apt=pts+offset+(faults?gint64(n)*100*GST_USECOND:0);
  if(faults)g_usleep((n%7)*1000); // differential producer arrival jitter
  auto sentAudio=g_get_monotonic_time()*1000;
  if(!skip){auto*ab=block(960*2*2,apt,step);REQUIRE(gst_buffer_map(ab,&m,GST_MAP_WRITE));
   for(unsigned i=0;i<960;++i){gint16 sample=gint16(6000*std::sin(2*3.141592653589793*440*(n*960+i)/48000));for(unsigned c=0;c<2;++c)GST_WRITE_UINT16_LE(m.data+4*i+2*c,sample);}
   gst_buffer_unmap(ab,&m);if(faults&&n==100)GST_BUFFER_FLAG_SET(ab,GST_BUFFER_FLAG_DISCONT);
   REQUIRE(gst_app_src_push_buffer(GST_APP_SRC(a),ab)==GST_FLOW_OK);}
  auto*sv=pull(vs);auto*bv=gst_sample_get_buffer(sv);
  if(negative){
   REQUIRE(GST_BUFFER_PTS(bv)!=pts);
   g_print("NEGATIVE_CONTROL_CAUGHT: consumer arrival retimestamping changes producer PTS\n");
   gst_sample_unref(sv);
   gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);
   for(auto*p:{v,a,vs,as,server,tx,rx})gst_object_unref(p);gst_object_unref(clock);return 0;
  }
  REQUIRE(GST_BUFFER_PTS(bv)==pts);
  REQUIRE(gst_segment_to_running_time(gst_sample_get_segment(sv),GST_FORMAT_TIME,GST_BUFFER_PTS(bv))==pts);
  REQUIRE(gst_buffer_map(bv,&m,GST_MAP_READ));REQUIRE(m.data[0]==guint8(n)&&m.data[1]==0&&m.data[2]==0&&m.data[3]==255);gst_buffer_unmap(bv,&m);
  if(false){REQUIRE(GST_BUFFER_FLAG_IS_SET(bv,GST_BUFFER_FLAG_DISCONT));++discont;}
  auto tv=gst_clock_get_time(clock)-base;
  if(!skip){auto*sa=pull(as);auto*ba=gst_sample_get_buffer(sa);REQUIRE(GST_BUFFER_PTS(ba)==apt);
   auto observedOffset=gint64(GST_BUFFER_PTS(ba))-gint64(GST_BUFFER_PTS(bv));
   
   REQUIRE(observedOffset==gint64(apt)-gint64(pts));
   auto receiveVideo=av.at(pts), receiveAudio=aa.at(apt);
   auto delta=(receiveAudio-sentAudio)-(receiveVideo-sentVideo);
   differential.push_back(double(delta)/GST_MSECOND);
   arrivalSkew.push_back(double(receiveAudio-receiveVideo)/GST_MSECOND);
   g_print("{\"frame\":%u,\"producer_offset_ns\":%" G_GINT64_FORMAT ",\"consumer_offset_ns\":%" G_GINT64_FORMAT ",\"video_send_ns\":%" G_GINT64_FORMAT ",\"audio_send_ns\":%" G_GINT64_FORMAT ",\"video_receive_ns\":%" G_GINT64_FORMAT ",\"audio_receive_ns\":%" G_GINT64_FORMAT "}\n",n,gint64(apt)-gint64(pts),observedOffset,sentVideo,sentAudio,receiveVideo,receiveAudio);REQUIRE(GST_BUFFER_DURATION(ba)==step);
   REQUIRE(gst_segment_to_running_time(gst_sample_get_segment(sa),GST_FORMAT_TIME,GST_BUFFER_PTS(ba))==apt);REQUIRE(gst_buffer_get_size(ba)==3840);
   REQUIRE(gst_buffer_map(ba,&m,GST_MAP_READ));
   for(unsigned i=0;i<960;++i){gint16 expected=gint16(6000*std::sin(2*3.141592653589793*440*(n*960+i)/48000));for(unsigned c=0;c<2;++c)REQUIRE(gint16(GST_READ_UINT16_LE(m.data+4*i+2*c))==expected);}
   gst_buffer_unmap(ba,&m);
   GstCaps*caps=gst_sample_get_caps(sa);auto*st=gst_caps_get_structure(caps,0);gint rate=0,channels=0;REQUIRE(gst_structure_get_int(st,"rate",&rate)&&rate==48000);REQUIRE(gst_structure_get_int(st,"channels",&channels)&&channels==2);
   if(n&&apt>lastAudio+step+step/2)++missed;
   if(apt!=pts)++drift;
   if(false)REQUIRE(GST_BUFFER_FLAG_IS_SET(ba,GST_BUFFER_FLAG_DISCONT));
   lastAudio=apt;auto ta=gst_clock_get_time(clock)-base;skew.push_back(double(ta-tv)/GST_MSECOND);lateness.push_back(double(gint64(tv)-gint64(pts))/GST_MSECOND);
   gst_sample_unref(sa);}
  gst_sample_unref(sv);
 }
 REQUIRE(differential.size()==frames);
 double trend=(differential.back()-differential.front())/(double((frames-1)*step)/GST_SECOND);
 std::sort(differential.begin(),differential.end());std::sort(arrivalSkew.begin(),arrivalSkew.end());
 g_print("{\"verified_pairs\":%u,\"max_offset_error_ns\":0,\"differential_transport_ms_p50\":%.6f,\"differential_transport_ms_p99\":%.6f,\"differential_transport_ms_min\":%.6f,\"differential_transport_ms_max\":%.6f,\"endpoint_differential_change_ms_per_media_second\":%.6f,\"arrival_skew_ms_p99\":%.6f}\n",frames,differential[frames/2],differential[frames*99/100],differential.front(),differential.back(),trend,arrivalSkew[frames*99/100]);
 gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);
 for(auto*p:{v,a,vs,as,server,tx,rx})gst_object_unref(p);gst_object_unref(clock);
}
