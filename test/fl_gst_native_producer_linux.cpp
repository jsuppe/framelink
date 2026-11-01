#include "framelink_dmabuf.h"
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <gst/app/gstappsrc.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void run(bool nv12,bool missingReady=false){
 auto name="native-dmabuf-sink-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,nv12?FL_FORMAT_NV12:FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&c)==FL_OK);
 std::string format=nv12?"NV12":"RGBA";GError*error=nullptr;
 auto*p=gst_parse_launch(("appsrc name=in is-live=true format=time caps=video/x-raw(memory:DMABuf),format="+format+",width=64,height=32,framerate=30/1 ! framelinksink name=sink native-memory=true preserve-frame-metadata=true sync=false async=false enable-last-sample=false channel="+name).c_str(),&error);CHECK(p&&!error);
 auto*sink=gst_bin_get_by_name(GST_BIN(p),"sink");auto*in=gst_bin_get_by_name(GST_BIN(p),"in");CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstBuffer*b=nullptr,*other=nullptr,*blocked=nullptr;g_signal_emit_by_name(sink,"acquire-native-buffer",1000u,&b);CHECK(b);
 g_signal_emit_by_name(sink,"acquire-native-buffer",1000u,&other);CHECK(other);
 g_signal_emit_by_name(sink,"acquire-native-buffer",0u,&blocked);CHECK(!blocked);
 auto*copy=gst_buffer_copy(b);CHECK(copy&&gst_buffer_peek_memory(copy,0)==gst_buffer_peek_memory(b,0));gst_buffer_unref(copy);
 auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_dmabuf_memory(mem));int fd=gst_dmabuf_memory_get_fd(mem);
 auto*layout=gst_buffer_get_video_meta(b);CHECK(layout);
 dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 void* data=mmap(nullptr,mem->size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(data!=MAP_FAILED);
 for(unsigned plane=0;plane<(nv12?2u:1u);++plane)for(unsigned y=0;y<(plane?16u:32u);++y)
     memset((char*)data+layout->offset[plane]+y*layout->stride[plane],71,nv12?64:256);
 CHECK(munmap(data,mem->size)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);gboolean accepted=FALSE;
 if(!missingReady){
  g_signal_emit_by_name(sink,"set-native-ready-fd",b,-2,&accepted);CHECK(!accepted);
  g_signal_emit_by_name(sink,"set-native-ready-fd",b,ready,&accepted);CHECK(accepted);
  g_signal_emit_by_name(sink,"set-native-ready-fd",b,-1,&accepted);CHECK(!accepted);
 }
 GST_BUFFER_PTS(b)=1000;GST_BUFFER_DTS(b)=900;GST_BUFFER_DURATION(b)=12345;
 CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),gst_buffer_ref(b))==GST_FLOW_OK);flFrame frame{};
 if(missingReady){auto*bus=gst_element_get_bus(p);auto*e=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GST_MESSAGE_ERROR);CHECK(e);gst_message_unref(e);gst_object_unref(bus);CHECK(flAcquireFrame(c,&frame,20)==FL_TIMEOUT);}
 else {
  CHECK(flAcquireFrame(c,&frame,20)==FL_TIMEOUT);uint64_t one=1;CHECK(write(ready,&one,sizeof(one))==sizeof(one));
  CHECK(flAcquireFrame(c,&frame,1000)==FL_OK&&frame.ptsNs==1000);
  flDmabuf native{};CHECK(flImageDmabuf(frame.image,&native)==FL_OK);struct stat original{};CHECK(fstat(fd,&original)==0);
  for(unsigned i=0;i<native.planes;++i){struct stat received{};CHECK(fstat(native.fd[i],&received)==0&&received.st_dev==original.st_dev&&received.st_ino==original.st_ino);}
  flFrameMetadata metadata{};CHECK(flGetFrameMetadata(c,&frame,&metadata)==FL_OK&&metadata.dtsNs==900&&metadata.durationNs==12345);
  flMappedImage map{};CHECK(flImageMapPlanes(frame.image,&map)==FL_OK);
  for(unsigned plane=0;plane<map.planes;++plane)for(unsigned y=0;y<map.plane[plane].rows;++y)for(unsigned x=0;x<(nv12?64u:256u);++x)CHECK(((unsigned char*)map.plane[plane].pixels)[y*map.plane[plane].stride+x]==71);
  flImageUnmap(frame.image);CHECK(flRelease(c,&frame)==FL_OK);
  g_signal_emit_by_name(sink,"acquire-native-buffer",0u,&blocked);CHECK(!blocked);
 }
 close(ready);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(in);gst_object_unref(sink);gst_object_unref(p);
 CHECK(gst_is_dmabuf_memory(gst_buffer_peek_memory(b,0)));gst_buffer_unref(b);gst_buffer_unref(other);flClose(c);
 printf("%s native DMA-BUF sink %s: bounded original allocations, explicit readiness, metadata and retained shutdown pass\n",format.c_str(),missingReady?"missing-ready negative":"roundtrip");
}
int main(int argc,char**argv){gst_init(&argc,&argv);CHECK(argc==2);GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);run(false);run(true);run(false,true);}
