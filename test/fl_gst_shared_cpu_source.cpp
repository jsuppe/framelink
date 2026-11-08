#include "framelink_session_transport.h"
#include "framelink_meta.h"
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/base/gstbasesrc.h>
#include <sys/socket.h>
#include <thread>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
extern "C" { GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements); }
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
#else
 CHECK(argc>1);GError*err=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&err);CHECK(plugin&&!err);gst_object_unref(plugin);
#endif
 for(unsigned mode=0;mode<8;mode++){
  const bool nv12=mode==1;auto*s=fl_gst_session_new(1);flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.video={nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8,18,16,60,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
  flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]=cfg.video;CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=1000000;
  if(mode!=3)CHECK(fl_gst_session_set_mapping(s,1,&mapping,0)==FL_GST_SESSION_READY);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=cfg.video.format;v.width=18;v.height=16;v.planeCount=nv12?2:1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=nv12?32:80;v.allocationBytes=v.planeStride[0]*16;if(nv12){v.planeOffset[1]=v.allocationBytes+64;v.planeStride[1]=32;v.allocationBytes+=64+32*8;}}
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;req.allowedOperations=FL_MEMORY_COPY;
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));flSharedCpuChannel*p=nullptr;flResult opened=FL_INVALID;
  std::thread setup([&]{opened=flSharedCpuOpenProducerFdEx(fd[1],mode==7?60000:1000,&cfg,&req,0,FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS,&p);});
  CHECK(fl_gst_session_bind_shared_cpu_fd(s,FALSE,1,fd[0],mode==7?60000:1000,&req,mode==2?0:FL_MEMORY_COPY,d,2)==FL_GST_SESSION_READY);setup.join();CHECK(opened==FL_OK);
  GError*error=nullptr;auto*pipeline=gst_parse_launch("framelinksrc shared-cpu=true name=source ! appsink name=output sync=false",&error);CHECK(pipeline&&!error);
  auto*source=gst_bin_get_by_name(GST_BIN(pipeline),"source");auto*output=gst_bin_get_by_name(GST_BIN(pipeline),"output");auto*context=fl_gst_session_create_context(s);gst_element_set_context(source,context);gst_context_unref(context);
  auto*bus=gst_element_get_bus(pipeline);
  if(mode==2){CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE);}
  else{
   CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
   if(mode==7){g_usleep(50000);CHECK(gst_pad_get_task_state(GST_BASE_SRC_PAD(source))==GST_TASK_STARTED);auto start=g_get_monotonic_time();CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);CHECK(g_get_monotonic_time()-start<1000000);}
   else {
    const unsigned frames=mode<2?8:mode==6?0:1;
    std::thread writer([&]{
     if(mode==4){flSharedCpuClose(p);p=nullptr;return;}
     for(unsigned n=1;n<=frames;n++){
      flSharedCpuLease*l=nullptr;flMappedImage v{};CHECK(flSharedCpuAcquireWrite(p,0,&l,&v)==FL_OK);
      for(unsigned plane=0;plane<v.planes;plane++)for(unsigned y=0;y<v.plane[plane].rows;y++)memset(static_cast<unsigned char*>(v.plane[plane].pixels)+y*v.plane[plane].stride,n+plane*20,v.plane[plane].rowBytes);
      flFrameMetadata m{};m.version=1;m.flags=FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|(n==4?FL_FRAME_DISCONT:0);m.dtsNs=n*1000-10;m.durationNs=1000;m.streamGeneration=mode==5?2:1;
      CHECK(flSharedCpuSubmitWithMetadata(l,n,n*1000,&m)==FL_OK);auto release=flSharedCpuReceiveRelease(p);CHECK(release==FL_OK||mode>=3);
     }
     if(mode<2||mode==6)CHECK(flSharedCpuSendEos(p)==FL_OK);
    });
    GstSample*held[3]{};
    if(mode<2)for(unsigned n=1;n<=8;n++){
     auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(output),3*GST_SECOND);CHECK(sample);auto*b=gst_sample_get_buffer(sample);
     CHECK(GST_BUFFER_PTS(b)==1000000+n*1000&&GST_BUFFER_DTS(b)==1000000+n*1000-10&&GST_BUFFER_DURATION(b)==1000);
     auto*m=gst_buffer_get_framelink_frame_meta(b);CHECK(m&&m->frame.streamGeneration==1&&m->frame.dtsNs==gint64(GST_BUFFER_DTS(b)));
     CHECK(bool(GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT))==(n==1||n==4));
     GstVideoInfo vi;CHECK(gst_video_info_from_caps(&vi,gst_sample_get_caps(sample)));GstVideoFrame frame{};CHECK(gst_video_frame_map(&frame,&vi,b,GST_MAP_READ));
     for(unsigned plane=0;plane<(nv12?2u:1u);plane++)for(unsigned y=0;y<(plane?8u:16u);y++)for(unsigned x=0;x<(nv12?18u:72u);x++)CHECK(static_cast<unsigned char*>(GST_VIDEO_FRAME_PLANE_DATA(&frame,plane))[y*GST_VIDEO_FRAME_PLANE_STRIDE(&frame,plane)+x]==n+plane*20);
     gst_video_frame_unmap(&frame);if(n<=3)held[n-1]=sample;else gst_sample_unref(sample);
    }
    writer.join();
    auto*message=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));CHECK(message);CHECK(GST_MESSAGE_TYPE(message)==(mode<2||mode==6?GST_MESSAGE_EOS:GST_MESSAGE_ERROR));gst_message_unref(message);
    // Exhaust both cache slots and retain a fallback through source teardown.
    CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
    for(unsigned i=0;i<3;i++)if(held[i]){guint8 first=0;CHECK(gst_buffer_extract(gst_sample_get_buffer(held[i]),0,&first,1)==1&&first==i+1);gst_sample_unref(held[i]);}
   }
  }
  gst_element_set_state(pipeline,GST_STATE_NULL);if(p)flSharedCpuClose(p);gst_object_unref(bus);gst_object_unref(source);gst_object_unref(output);gst_object_unref(pipeline);g_object_unref(s);
 }
 puts("shared CPU source: padded BGRA/NV12, mapped timestamps/metadata, retained copies, EOS, policy/mapping/generation/death negatives and cancellation passed");
}
