#include "framelink_session_transport.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <sys/socket.h>
#include <thread>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static GstElement* pipeline(const char*text){GError*e=nullptr;auto*p=gst_parse_launch(text,&e);CHECK(p&&!e);return p;}
static void context(GstElement*e,FlGstSession*s){auto*c=fl_gst_session_create_context(s);gst_element_set_context(e,c);gst_context_unref(c);}
static void terminal(GstElement*p,GstMessageType type){auto*b=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(b,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));if(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*debug=nullptr;gst_message_parse_error(m,&e,&debug);fprintf(stderr,"bus: %s %s\n",e->message,debug?debug:"");g_free(debug);g_clear_error(&e);}CHECK(m&&GST_MESSAGE_TYPE(m)==type);gst_message_unref(m);gst_object_unref(b);}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
#else
 CHECK(argc>1);GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
#endif
 for(unsigned mode=0;mode<6;mode++){
  fprintf(stderr,"mode %u\n",mode);bool nv12=mode%2==1;auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
  flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;cfg.video={nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8,18,16,60,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
  flSessionCapabilities offers{};offers.version=2;offers.videoCount=1;offers.video[0]=cfg.video;offers.audioCount=1;offers.audio[0]=cfg.audio;
  CHECK(fl_gst_session_configure(s,&cfg,&offers,&offers)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(r,&cfg,&offers,&offers)==FL_GST_SESSION_READY);
  int ev[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,ev));auto*sa=g_socket_new_from_fd(ev[0],nullptr);auto*sb=g_socket_new_from_fd(ev[1],nullptr);CHECK(sa&&sb);auto*ea=g_socket_connection_factory_create_connection(sa);auto*eb=g_socket_connection_factory_create_connection(sb);g_object_unref(sa);g_object_unref(sb);
  guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);gboolean auth=FALSE;
  std::thread authenticate([&]{auth=fl_gst_session_authenticate_event_stream(s,ea,TRUE,key,1000,nullptr,nullptr);});CHECK(fl_gst_session_authenticate_event_stream(r,eb,FALSE,key,1000,nullptr,nullptr));authenticate.join();CHECK(auth);g_bytes_unref(key);g_object_unref(ea);g_object_unref(eb);
  flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=1000000;CHECK(fl_gst_session_set_mapping(r,1,&mapping,0)==FL_GST_SESSION_READY);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=cfg.video.format;v.width=18;v.height=16;v.planeCount=nv12?2:1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=nv12?32:80;v.allocationBytes=v.planeStride[0]*16;if(nv12){v.planeOffset[1]=v.allocationBytes+64;v.planeStride[1]=32;v.allocationBytes+=64+256;}}
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;req.allowedOperations=FL_MEMORY_COPY;
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));FlGstSessionResult opened=FL_GST_SESSION_INVALID;
  std::thread setup([&]{opened=fl_gst_session_bind_shared_cpu_fd(s,TRUE,1,fd[1],60000,&req,FL_MEMORY_COPY,nullptr,0);});
  CHECK(fl_gst_session_bind_shared_cpu_fd(r,FALSE,1,fd[0],60000,&req,FL_MEMORY_COPY,d,2)==FL_GST_SESSION_READY);setup.join();CHECK(opened==FL_GST_SESSION_READY);
  // Independently authenticated PCM stream, sharing only session/clock identity.
  int af[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,af));
  auto*as=g_socket_new_from_fd(af[0],nullptr);auto*ar=g_socket_new_from_fd(af[1],nullptr);
  auto*ac=g_socket_connection_factory_create_connection(as);auto*bc=g_socket_connection_factory_create_connection(ar);g_object_unref(as);g_object_unref(ar);
  key=g_bytes_new(secret,32);auth=FALSE;
  std::thread aa([&]{auth=fl_gst_session_authenticate_audio_stream(s,ac,TRUE,key,1000,nullptr,nullptr);});
  CHECK(fl_gst_session_authenticate_audio_stream(r,bc,FALSE,key,1000,nullptr,nullptr));aa.join();CHECK(auth);g_bytes_unref(key);g_object_unref(ac);
  auto*tx=pipeline("appsrc name=vi is-live=true format=time ! framelinksink shared-cpu=true appsrc name=ai is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av");
  auto*rx=pipeline("framelinksrc shared-cpu=true ! appsink name=vo sync=false async=false framelinkaudiosrc tracks=av ! appsink name=ao sync=false async=false");
  context(tx,s);context(rx,r);
  auto*vi=gst_bin_get_by_name(GST_BIN(tx),"vi");auto*ai=gst_bin_get_by_name(GST_BIN(tx),"ai");auto*vo=gst_bin_get_by_name(GST_BIN(rx),"vo");auto*ao=gst_bin_get_by_name(GST_BIN(rx),"ao");
  GstVideoInfo info;gst_video_info_set_format(&info,nv12?GST_VIDEO_FORMAT_NV12:GST_VIDEO_FORMAT_BGRA,18,16);auto*vc=gst_video_info_to_caps(&info);gst_app_src_set_caps(GST_APP_SRC(vi),vc);gst_caps_unref(vc);
  CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  const unsigned count=(mode==2||mode==3)?0:8;
  for(unsigned n=0;n<count;n++){
   auto*v=gst_buffer_new_allocate(nullptr,info.size,nullptr);gst_buffer_memset(v,0,n+1,info.size);GST_BUFFER_PTS(v)=37000000+n*1000000;GST_BUFFER_DURATION(v)=1000000;
   auto*a=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(a,0,n+31,192);GST_BUFFER_PTS(a)=n*1000000;GST_BUFFER_DURATION(a)=1000000;
   CHECK(gst_app_src_push_buffer(GST_APP_SRC(vi),v)==GST_FLOW_OK);CHECK(gst_app_src_push_buffer(GST_APP_SRC(ai),a)==GST_FLOW_OK);
   auto*vs=gst_app_sink_try_pull_sample(GST_APP_SINK(vo),2*GST_SECOND);auto*ap=gst_app_sink_try_pull_sample(GST_APP_SINK(ao),2*GST_SECOND);CHECK(vs&&ap);
   auto*vb=gst_sample_get_buffer(vs);auto*ab=gst_sample_get_buffer(ap);
   CHECK(GST_BUFFER_PTS(vb)==38000000+n*1000000&&GST_BUFFER_PTS(ab)==1000000+n*1000000);
   CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000&&GST_BUFFER_DURATION(ab)==1000000&&GST_BUFFER_DURATION(vb)==1000000);
   guint8 pcm[192];CHECK(gst_buffer_extract(ab,0,pcm,192)==192);for(auto b:pcm)CHECK(b==n+31);
   GstVideoInfo got;CHECK(gst_video_info_from_caps(&got,gst_sample_get_caps(vs)));GstVideoFrame vf{};CHECK(gst_video_frame_map(&vf,&got,vb,GST_MAP_READ));
   for(unsigned p=0;p<(nv12?2u:1u);p++)for(unsigned y=0;y<(p?8u:16u);y++)for(unsigned x=0;x<(nv12?18u:72u);x++)CHECK(static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&vf,p))[y*GST_VIDEO_FRAME_PLANE_STRIDE(&vf,p)+x]==n+1);
   gst_video_frame_unmap(&vf);gst_sample_unref(vs);gst_sample_unref(ap);
  }
  if(mode==4){
   // Contradictory independently received terminal accounting must not become
   // successful aggregate EOS, even after every payload was delivered.
   CHECK(fl_gst_session_video_eos(r,1,count+1,37000000+(count-1)*1000000)==FL_GST_SESSION_READY);
   CHECK(gst_app_src_end_of_stream(GST_APP_SRC(vi))==GST_FLOW_OK);
   CHECK(gst_app_src_end_of_stream(GST_APP_SRC(ai))==GST_FLOW_OK);
   terminal(rx,GST_MESSAGE_ERROR);
  }else if(mode==5){
   // Loss of authenticated PCM transport is not an empty/successful terminal.
   CHECK(g_socket_shutdown(g_socket_connection_get_socket(bc),TRUE,TRUE,nullptr));
   terminal(rx,GST_MESSAGE_ERROR);
  }else{
  // Queue audio-first or video-first termination; neither may finish the whole
  // receiving pipeline until the other track is terminated.
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?vi:ai))==GST_FLOW_OK);
  auto*bus=gst_element_get_bus(rx);CHECK(!gst_bus_timed_pop_filtered(bus,30*GST_MSECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR)));gst_object_unref(bus);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?ai:vi))==GST_FLOW_OK);
  terminal(tx,GST_MESSAGE_EOS);terminal(rx,GST_MESSAGE_EOS);
  guint64 total=999;gint64 last=999;CHECK(fl_gst_session_wait_video_eos(r,1,0,&total,&last)==FL_GST_SESSION_READY&&total==count&&last==(count?gint64(37000000+(count-1)*1000000):gint64(-1)));
  }
  auto stop=g_get_monotonic_time();gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);CHECK(g_get_monotonic_time()-stop<1000000);g_object_unref(bc);
  gst_object_unref(vi);gst_object_unref(ai);gst_object_unref(vo);gst_object_unref(ao);gst_object_unref(tx);gst_object_unref(rx);g_object_unref(s);g_object_unref(r);
 }
 puts("CPU A/V: exact PCM/pixels, common mapping preserves37ms offset, both EOS submission orders, empty streams, terminal conflict and PCM death passed");
}
