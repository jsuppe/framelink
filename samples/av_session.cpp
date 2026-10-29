// Local session example: FrameLink video + existing GStreamer GDP/TCP PCM.
// No audio additions to flFrame; both pipelines use one explicit clock/epoch.
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
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
int main(int argc,char**argv) {
 gst_init(&argc,&argv);
 bool faults=false, play=false; unsigned frames=150;
 REQUIRE(argc>=2);
 for(int i=2;i<argc;++i) {
  if(!strcmp(argv[i],"--faults")) faults=true;
  else if(!strcmp(argv[i],"--play")) play=true;
  else { g_printerr("usage: %s plugin-path [--faults] [--play]\n",argv[0]);return 2; }
 }
 GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);if(e)g_printerr("%s\n",e->message);REQUIRE(plugin);gst_object_unref(plugin);
 auto session=std::to_string(g_get_monotonic_time());auto channel="av-"+session;
 // GDP carries caps, timestamps and buffer flags; TCP is loopback only.
 // max-size-time bounds media queues; TCP's own kernel buffering is not a latency guarantee.
 auto*tx=pipeline("appsrc name=video is-live=true format=time block=true max-buffers=2 caps=video/x-raw,format=BGRA,width=160,height=90,framerate=50/1 ! framelinksink preserve-timestamps=true channel="+channel+
  " appsrc name=audio is-live=true format=time block=true max-buffers=2 caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! queue max-size-buffers=4 max-size-bytes=0 max-size-time=80000000 ! gdppay ! tcpserversink name=server host=127.0.0.1 port=0 sync=false buffers-max=8");
 auto*clock=gst_system_clock_obtain();guint64 base=gst_clock_get_time(clock);
 auto start=[&](GstElement*p){gst_pipeline_use_clock(GST_PIPELINE(p),clock);gst_element_set_start_time(p,GST_CLOCK_TIME_NONE);gst_element_set_base_time(p,base);REQUIRE(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);};
 start(tx);auto*server=gst_bin_get_by_name(GST_BIN(tx),"server");gint port=0;
 for(int i=0;i<100&&!port;++i){g_object_get(server,"current-port",&port,nullptr);g_usleep(10000);}REQUIRE(port>0);
 std::string vo="appsink name=vout sync=true max-buffers=2",ao="appsink name=aout sync=true max-buffers=2";
 if(play){vo="tee name=vt vt. ! queue max-size-buffers=2 ! videoconvert ! autovideosink sync=true vt. ! queue max-size-buffers=2 ! "+vo;
 ao="tee name=at at. ! queue max-size-buffers=4 ! audioconvert ! audioresample ! autoaudiosink sync=true at. ! queue max-size-buffers=4 ! "+ao;}
 auto*rx=pipeline("framelinksrc channel="+channel+" width=160 height=90 fps=50 preserve-timestamps=true ! "+vo+
  " tcpclientsrc host=127.0.0.1 port="+std::to_string(port)+" ! gdpdepay ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! "+ao);
 start(rx);
 auto*v=gst_bin_get_by_name(GST_BIN(tx),"video");auto*a=gst_bin_get_by_name(GST_BIN(tx),"audio");auto*vs=gst_bin_get_by_name(GST_BIN(rx),"vout");auto*as=gst_bin_get_by_name(GST_BIN(rx),"aout");
 g_usleep(150000); // allow loopback accept; not part of measured stream
 const guint64 epoch=gst_clock_get_time(clock)-base+100*GST_MSECOND,step=20*GST_MSECOND;
 unsigned missed=0,drift=0,discont=0;guint64 lastAudio=0;std::vector<double>skew,lateness;
 for(unsigned n=0;n<frames;++n){
  // Deliberate restart resets media running time; do not hide it by arrival retimestamping.
  guint64 pts=epoch+(faults&&n>=100?n-100:n)*step;
  auto*vb=block(160*90*4,pts,step);GstMapInfo m{};REQUIRE(gst_buffer_map(vb,&m,GST_MAP_WRITE));
  for(unsigned y=0;y<90;++y)for(unsigned x=0;x<160;++x){auto*p=m.data+4*(y*160+x);p[0]=guint8(n);p[1]=guint8(x);p[2]=guint8(y);p[3]=255;}gst_buffer_unmap(vb,&m);
  REQUIRE(gst_app_src_push_buffer(GST_APP_SRC(v),vb)==GST_FLOW_OK);
  bool skip=faults&&n==30;guint64 apt=pts+(faults&&n>=60&&n<100?(n-59)*100*GST_USECOND:0);
  if(!skip){auto*ab=block(960*2*2,apt,step);REQUIRE(gst_buffer_map(ab,&m,GST_MAP_WRITE));
   for(unsigned i=0;i<960;++i){gint16 sample=gint16(6000*std::sin(2*3.141592653589793*440*(n*960+i)/48000));for(unsigned c=0;c<2;++c)GST_WRITE_UINT16_LE(m.data+4*i+2*c,sample);}
   gst_buffer_unmap(ab,&m);if(faults&&n==100)GST_BUFFER_FLAG_SET(ab,GST_BUFFER_FLAG_DISCONT);
   REQUIRE(gst_app_src_push_buffer(GST_APP_SRC(a),ab)==GST_FLOW_OK);}
  auto*sv=pull(vs);auto*bv=gst_sample_get_buffer(sv);REQUIRE(GST_BUFFER_PTS(bv)==pts);
  REQUIRE(gst_segment_to_running_time(gst_sample_get_segment(sv),GST_FORMAT_TIME,GST_BUFFER_PTS(bv))==pts);
  REQUIRE(gst_buffer_map(bv,&m,GST_MAP_READ));REQUIRE(m.data[0]==guint8(n)&&m.data[1]==0&&m.data[2]==0&&m.data[3]==255);gst_buffer_unmap(bv,&m);
  if(n==100&&faults){REQUIRE(GST_BUFFER_FLAG_IS_SET(bv,GST_BUFFER_FLAG_DISCONT));++discont;}
  auto tv=gst_clock_get_time(clock)-base;
  if(!skip){auto*sa=pull(as);auto*ba=gst_sample_get_buffer(sa);REQUIRE(GST_BUFFER_PTS(ba)==apt);REQUIRE(GST_BUFFER_DURATION(ba)==step);
   REQUIRE(gst_segment_to_running_time(gst_sample_get_segment(sa),GST_FORMAT_TIME,GST_BUFFER_PTS(ba))==apt);REQUIRE(gst_buffer_get_size(ba)==3840);
   REQUIRE(gst_buffer_map(ba,&m,GST_MAP_READ));
   for(unsigned i=0;i<960;++i){gint16 expected=gint16(6000*std::sin(2*3.141592653589793*440*(n*960+i)/48000));for(unsigned c=0;c<2;++c)REQUIRE(gint16(GST_READ_UINT16_LE(m.data+4*i+2*c))==expected);}
   gst_buffer_unmap(ba,&m);
   GstCaps*caps=gst_sample_get_caps(sa);auto*st=gst_caps_get_structure(caps,0);gint rate=0,channels=0;REQUIRE(gst_structure_get_int(st,"rate",&rate)&&rate==48000);REQUIRE(gst_structure_get_int(st,"channels",&channels)&&channels==2);
   if(n&&apt>lastAudio+step+step/2)++missed;
   if(apt!=pts)++drift;
   if(n==100&&faults)REQUIRE(GST_BUFFER_FLAG_IS_SET(ba,GST_BUFFER_FLAG_DISCONT));
   lastAudio=apt;auto ta=gst_clock_get_time(clock)-base;skew.push_back(double(ta-tv)/GST_MSECOND);lateness.push_back(double(gint64(tv)-gint64(pts))/GST_MSECOND);
   gst_sample_unref(sa);}
  gst_sample_unref(sv);
 }
 REQUIRE(missed==(faults?1u:0u));REQUIRE(drift==(faults?40u:0u));REQUIRE(discont==(faults?1u:0u));
 std::sort(skew.begin(),skew.end());std::sort(lateness.begin(),lateness.end());
 g_print("session=%s clock=GstSystemClock shared-base=%" G_GUINT64_FORMAT " video=%u audio=%u missing=%u drift_blocks=%u restarts=%u\n",session.c_str(),base,frames,frames-(faults?1:0),missed,drift,discont);
 g_print("app-observed pull skew ms p50=%.3f p99=%.3f; video scheduled lateness ms p50=%.3f p99=%.3f (not speaker/scanout latency)\n",skew[skew.size()/2],skew[skew.size()*99/100],lateness[lateness.size()/2],lateness[lateness.size()*99/100]);
 gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);
 for(auto*p:{v,a,vs,as,server,tx,rx})gst_object_unref(p);gst_object_unref(clock);
}
