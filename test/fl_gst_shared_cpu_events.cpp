#include "framelink_session_transport.h"
#include <gst/app/gstappsink.h>
#include <sys/socket.h>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
struct Pair{GSocketConnection*a,*b;Pair(){int f[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,f));auto*x=g_socket_new_from_fd(f[0],nullptr);auto*y=g_socket_new_from_fd(f[1],nullptr);CHECK(x&&y);a=g_socket_connection_factory_create_connection(x);b=g_socket_connection_factory_create_connection(y);g_object_unref(x);g_object_unref(y);}~Pair(){g_object_unref(a);g_object_unref(b);}};
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
#else
 CHECK(argc>1);auto*plugin=gst_plugin_load_file(argv[1],nullptr);CHECK(plugin);gst_object_unref(plugin);
#endif
 for(unsigned mode=0;mode<7;mode++){
  fprintf(stderr,"event mode %u\n",mode);Pair events;auto*s=fl_gst_session_new(1);auto*r=fl_gst_session_new(1);
  flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.video={FL_FORMAT_BGRA8,16,16,60,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=1000;cfg.idleTimeoutMs=mode==6?60000:1000;
  flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]=cfg.video;CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(r,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);gboolean auth=FALSE;
  std::thread authenticate([&]{auth=fl_gst_session_authenticate_event_stream(s,events.a,TRUE,key,1000,nullptr,nullptr);});CHECK(fl_gst_session_authenticate_event_stream(r,events.b,FALSE,key,1000,nullptr,nullptr));authenticate.join();CHECK(auth);g_bytes_unref(key);
  flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=1000000;CHECK(fl_gst_session_set_mapping(r,1,&mapping,0)==FL_GST_SESSION_READY);
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;req.allowedOperations=FL_MEMORY_COPY;
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=1024;}
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,fd));flSharedCpuChannel*p=nullptr;flResult opened=FL_INVALID;
  std::thread open([&]{opened=flSharedCpuOpenProducerFdEx(fd[1],60000,&cfg,&req,0,3,&p);});CHECK(fl_gst_session_bind_shared_cpu_fd(r,FALSE,1,fd[0],60000,&req,FL_MEMORY_COPY,d,2)==FL_GST_SESSION_READY);open.join();CHECK(opened==FL_OK);
  CHECK(flSharedCpuWaitReadable(p,0)==FL_TIMEOUT);CHECK(flSharedCpuWaitReadable(p,60001)==FL_INVALID);
  flSessionConfig snapshot{};uint32_t ops=99,flags=0,slots=0;int role=0;CHECK(flSharedCpuQuery(p,&snapshot,&ops,&flags,&role,&slots)==FL_OK&&snapshot.generation==1&&role&&slots==2);
  auto*pipeline=gst_parse_launch("framelinksrc shared-cpu=true name=source ! appsink name=output sync=false",nullptr);CHECK(pipeline);auto*source=gst_bin_get_by_name(GST_BIN(pipeline),"source");auto*output=gst_bin_get_by_name(GST_BIN(pipeline),"output");auto*context=fl_gst_session_create_context(r);gst_element_set_context(source,context);gst_context_unref(context);
  std::atomic<unsigned> gaps{0};auto*pad=gst_element_get_static_pad(output,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,[](GstPad*,GstPadProbeInfo*info,gpointer data){auto*e=GST_PAD_PROBE_INFO_EVENT(info);if(GST_EVENT_TYPE(e)==GST_EVENT_GAP){GstClockTime pts,duration;gst_event_parse_gap(e,&pts,&duration);CHECK((pts==1000500||pts==1001500)&&duration==100);++*static_cast<std::atomic<unsigned>*>(data);}return GST_PAD_PROBE_OK;},&gaps,nullptr);gst_object_unref(pad);
  CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  auto send=[&](guint kind,guint64 count,guint64 pts,guint64 duration=0){FlGstVideoEvent event{kind,count,pts,duration};CHECK(fl_gst_session_send_video_event(s,1,&event)==FL_GST_SESSION_READY);};
  auto start=g_get_monotonic_time();
  if(mode==6){g_usleep(50000);start=g_get_monotonic_time();send(FL_GST_VIDEO_FLUSH,0,0);}
  else{
   send(FL_GST_VIDEO_GAP,mode==4?1:0,500,100);send(FL_GST_VIDEO_BARRIER,mode==1?1:0,mode==2?999:1000);
   flSharedCpuLease*w=nullptr;flMappedImage view{};CHECK(flSharedCpuAcquireWrite(p,0,&w,&view)==FL_OK);memset(view.plane[0].pixels,73,1024);flFrameMetadata meta{};meta.version=1;meta.flags=FL_FRAME_DISCONT;meta.streamGeneration=1;CHECK(flSharedCpuSubmitWithMetadata(w,1,1000,&meta)==FL_OK);CHECK(flSharedCpuWaitReadable(p,1000)==FL_OK);CHECK(flSharedCpuReceiveRelease(p)==FL_OK);
   if(mode==0||mode==3||mode==5){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(output),2*GST_SECOND);CHECK(sample);CHECK(GST_BUFFER_PTS(gst_sample_get_buffer(sample))==1001000&&gaps==1);gst_sample_unref(sample);CHECK(flSharedCpuSendEos(p)==FL_OK);send(FL_GST_VIDEO_GAP,1,1500,100);send(FL_GST_VIDEO_END,mode==3?2:1,mode==5?999:1000);}
  }
  auto*bus=gst_element_get_bus(pipeline);auto*message=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));CHECK(message&&GST_MESSAGE_TYPE(message)==(mode==0?GST_MESSAGE_EOS:GST_MESSAGE_ERROR));if(mode==0)CHECK(gaps==2);if(mode==6)CHECK(g_get_monotonic_time()-start<1000000);gst_message_unref(message);gst_object_unref(bus);
  gst_element_set_state(pipeline,GST_STATE_NULL);flSharedCpuClose(p);gst_object_unref(source);gst_object_unref(output);gst_object_unref(pipeline);g_object_unref(s);g_object_unref(r);
 }
 puts("CPU events: authenticated GAP/barrier/END mapping, strict frame-count/PTS negatives and bounded idle FLUSH cancellation passed");
}
