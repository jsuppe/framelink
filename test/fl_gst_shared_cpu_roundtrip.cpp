#include "framelink_session_transport.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <thread>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
#include "fl_gst_cpu_test_pair.hpp"
static GstElement* pipeline(const char*text){GError*e=nullptr;auto*p=gst_parse_launch(text,&e);CHECK(p&&!e);return p;}
static void context(GstElement*e,FlGstSession*s){auto*c=fl_gst_session_create_context(s);gst_element_set_context(e,c);gst_context_unref(c);}
static void terminal(GstElement*p,GstMessageType type){auto*b=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(b,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));if(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*debug=nullptr;gst_message_parse_error(m,&e,&debug);fprintf(stderr,"bus: %s %s\n",e->message,debug?debug:"");g_free(debug);g_clear_error(&e);}CHECK(m&&GST_MESSAGE_TYPE(m)==type);gst_message_unref(m);gst_object_unref(b);}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
#else
 CHECK(argc>1);GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
#endif
 for(unsigned mode=0;mode<8;mode++){
  fprintf(stderr,"mode %u\n",mode);bool nv12=mode==1;auto*s=fl_gst_session_new(1);auto*r=fl_gst_session_new(1);
  flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.video={uint32_t(nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8),18,16,60,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
  flSessionCapabilities offers{};offers.version=2;offers.videoCount=1;offers.video[0]=cfg.video;
  CHECK(fl_gst_session_configure(s,&cfg,&offers,&offers)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(r,&cfg,&offers,&offers)==FL_GST_SESSION_READY);
#ifdef _WIN32
  auto*listener=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(listener,nullptr,nullptr);CHECK(port);
  GSocketConnection*ea=nullptr;std::thread accept([&]{ea=g_socket_listener_accept(listener,nullptr,nullptr,nullptr);});
  auto*eventClient=g_socket_client_new();auto*eb=g_socket_client_connect_to_host(eventClient,"127.0.0.1",port,nullptr,nullptr);accept.join();CHECK(ea&&eb);g_object_unref(eventClient);g_object_unref(listener);
#else
  int ev[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,ev));auto*sa=g_socket_new_from_fd(ev[0],nullptr);auto*sb=g_socket_new_from_fd(ev[1],nullptr);CHECK(sa&&sb);auto*ea=g_socket_connection_factory_create_connection(sa);auto*eb=g_socket_connection_factory_create_connection(sb);g_object_unref(sa);g_object_unref(sb);
#endif
  guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);gboolean auth=FALSE;
  std::thread authenticate([&]{auth=fl_gst_session_authenticate_event_stream(s,ea,TRUE,key,1000,nullptr,nullptr);});CHECK(fl_gst_session_authenticate_event_stream(r,eb,FALSE,key,1000,nullptr,nullptr));authenticate.join();CHECK(auth);g_bytes_unref(key);g_object_unref(ea);g_object_unref(eb);
  flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=1000000;CHECK(fl_gst_session_set_mapping(r,1,&mapping,0)==FL_GST_SESSION_READY);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=cfg.video.format;v.width=18;v.height=16;v.planeCount=nv12?2:1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=nv12?32:80;v.allocationBytes=v.planeStride[0]*16;if(nv12){v.planeOffset[1]=v.allocationBytes+64;v.planeStride[1]=32;v.allocationBytes+=64+256;}}
#ifdef _WIN32
  // Windows section size is page-rounded; keep exact exported allocation size.
  SYSTEM_INFO systemInfo{};GetSystemInfo(&systemInfo);
  for(auto& descriptor:d)descriptor.allocationBytes=(descriptor.allocationBytes+systemInfo.dwPageSize-1)/systemInfo.dwPageSize*systemInfo.dwPageSize;
#endif
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;req.allowedOperations=FL_MEMORY_COPY;
  FlGstSessionResult opened=FL_GST_SESSION_INVALID;
#ifdef _WIN32
  CpuTestPair pair;auto client=pair.take(true),server=pair.take(false);
  std::thread setup([&]{opened=fl_gst_session_bind_shared_cpu_pipe(s,TRUE,1,client,GetCurrentProcess(),60000,&req,mode==2?0:FL_MEMORY_COPY,nullptr,0);});
  CHECK(fl_gst_session_bind_shared_cpu_pipe(r,FALSE,1,server,GetCurrentProcess(),60000,&req,FL_MEMORY_COPY,d,2)==FL_GST_SESSION_READY);
#else
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));
  std::thread setup([&]{opened=fl_gst_session_bind_shared_cpu_fd(s,TRUE,1,fd[1],60000,&req,mode==2?0:FL_MEMORY_COPY,nullptr,0);});
  CHECK(fl_gst_session_bind_shared_cpu_fd(r,FALSE,1,fd[0],60000,&req,FL_MEMORY_COPY,d,2)==FL_GST_SESSION_READY);
#endif
  setup.join();CHECK(opened==FL_GST_SESSION_READY);
  auto*tx=pipeline("appsrc name=input is-live=true format=time ! framelinksink name=send shared-cpu=true");auto*input=gst_bin_get_by_name(GST_BIN(tx),"input");auto*send=gst_bin_get_by_name(GST_BIN(tx),"send");context(send,s);
  GstVideoInfo info;gst_video_info_set_format(&info,nv12?GST_VIDEO_FORMAT_NV12:GST_VIDEO_FORMAT_BGRA,mode==3?20:18,16);auto*caps=gst_video_info_to_caps(&info);gst_app_src_set_caps(GST_APP_SRC(input),caps);gst_caps_unref(caps);
  GstElement*rx=nullptr,*output=nullptr;flSharedCpuChannel*raw=nullptr;
  if(mode==5){raw=fl_gst_session_claim_shared_cpu(r,FALSE,1);CHECK(raw);}
  else {rx=pipeline("framelinksrc shared-cpu=true name=receive ! appsink name=output sync=false");auto*receive=gst_bin_get_by_name(GST_BIN(rx),"receive");context(receive,r);gst_object_unref(receive);output=gst_bin_get_by_name(GST_BIN(rx),"output");CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);}
  if(mode==2)CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE);
  else{
   CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
   unsigned frames=mode<2?8:mode==6?0:1;
   for(unsigned n=1;n<=frames;n++){
    auto*b=gst_buffer_new_allocate(nullptr,info.size,nullptr);GstMapInfo map{};CHECK(gst_buffer_map(b,&map,GST_MAP_WRITE));memset(map.data,n,map.size);gst_buffer_unmap(b,&map);
    if(mode==7){auto*split=gst_buffer_new();auto*m=gst_buffer_peek_memory(b,0);gst_buffer_append_memory(split,gst_memory_share(m,0,info.size/2));gst_buffer_append_memory(split,gst_memory_share(m,info.size/2,info.size-info.size/2));gst_buffer_unref(b);b=split;CHECK(gst_buffer_n_memory(b)==2);}
    GST_BUFFER_PTS(b)=n*1000;GST_BUFFER_DTS(b)=n*1000-10;GST_BUFFER_DURATION(b)=mode==4?G_MAXUINT64-1:1000;if(n==4)GST_BUFFER_FLAG_SET(b,GST_BUFFER_FLAG_DISCONT);
    CHECK(gst_app_src_push_buffer(GST_APP_SRC(input),b)==GST_FLOW_OK);
   }
   CHECK(gst_app_src_end_of_stream(GST_APP_SRC(input))==GST_FLOW_OK);
   if(mode<2){for(unsigned n=1;n<=8;n++){
    auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(output),3*GST_SECOND);CHECK(sample);auto*b=gst_sample_get_buffer(sample);
    CHECK(GST_BUFFER_PTS(b)==1000000+n*1000&&GST_BUFFER_DTS(b)==1000000+n*1000-10&&GST_BUFFER_DURATION(b)==1000);
    GstVideoInfo v;CHECK(gst_video_info_from_caps(&v,gst_sample_get_caps(sample)));GstVideoFrame frame{};CHECK(gst_video_frame_map(&frame,&v,b,GST_MAP_READ));
    for(unsigned plane=0;plane<(nv12?2u:1u);plane++)for(unsigned y=0;y<(plane?8u:16u);y++)for(unsigned x=0;x<(nv12?18u:72u);x++)CHECK(static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&frame,plane))[y*GST_VIDEO_FRAME_PLANE_STRIDE(&frame,plane)+x]==n);
    gst_video_frame_unmap(&frame);gst_sample_unref(sample);
   }terminal(tx,GST_MESSAGE_EOS);terminal(rx,GST_MESSAGE_EOS);}
   else if(mode==5){
    flSharedCpuLease*held=nullptr,*none=nullptr;flMappedImage view{};uint64_t seq=0;int64_t pts=0;int eos=0;
    CHECK(flSharedCpuReceiveEvent(raw,&held,&view,&seq,&pts,&eos)==FL_OK&&!eos&&seq==1);
    CHECK(flSharedCpuReceiveEvent(raw,&none,&view,&seq,&pts,&eos)==FL_OK&&eos&&!none);
    auto*bus=gst_element_get_bus(tx);CHECK(!gst_bus_timed_pop_filtered(bus,50*GST_MSECOND,GST_MESSAGE_EOS));gst_object_unref(bus);
    auto start=g_get_monotonic_time();CHECK(gst_element_set_state(tx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);CHECK(g_get_monotonic_time()-start<1000000);
    CHECK(flSharedCpuReleaseRead(held)==FL_DISCONNECTED);
   }else if(mode==6){terminal(tx,GST_MESSAGE_EOS);terminal(rx,GST_MESSAGE_EOS);}
   else terminal(tx,GST_MESSAGE_ERROR);
  }
  gst_element_set_state(tx,GST_STATE_NULL);if(rx)gst_element_set_state(rx,GST_STATE_NULL);if(raw)flSharedCpuClose(raw);
  gst_object_unref(send);gst_object_unref(input);gst_object_unref(tx);if(output)gst_object_unref(output);if(rx)gst_object_unref(rx);g_object_unref(s);g_object_unref(r);
 }
 puts("CPU video roundtrip: BGRA/NV12 exact pixels/timing, COPY/caps/duration rejection, empty EOS and held-final-frame drain/cancellation passed");
}
