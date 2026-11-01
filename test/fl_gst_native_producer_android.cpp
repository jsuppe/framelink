#include "framelink_ahb.h"
#include "../gst/framelink_native.h"
#include <gst/app/gstappsrc.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void run(bool rgb10,bool missingReady=false){
 auto name="native-ahb-sink-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,rgb10?FL_FORMAT_RGB10A2:FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&c)==FL_OK);
 std::string format=rgb10?"RGB10A2_LE":"RGBA";GError*error=nullptr;
 auto*p=gst_parse_launch(("appsrc name=in is-live=true format=time caps=video/x-raw(memory:FrameLinkAHardwareBuffer),format="+format+",width=64,height=32,framerate=30/1 ! framelinksink name=sink native-memory=true preserve-frame-metadata=true sync=false async=false enable-last-sample=false channel="+name).c_str(),&error);CHECK(p&&!error);
 auto*sink=gst_bin_get_by_name(GST_BIN(p),"sink");auto*in=gst_bin_get_by_name(GST_BIN(p),"in");CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstBuffer*b=nullptr,*other=nullptr,*blocked=nullptr;g_signal_emit_by_name(sink,"acquire-native-buffer",1000u,&b);CHECK(b);
 g_signal_emit_by_name(sink,"acquire-native-buffer",1000u,&other);CHECK(other);
 g_signal_emit_by_name(sink,"acquire-native-buffer",0u,&blocked);CHECK(!blocked);
 auto*copy=gst_buffer_copy(b);CHECK(copy&&gst_buffer_peek_memory(copy,0)==gst_buffer_peek_memory(b,0));gst_buffer_unref(copy);
 auto*mem=gst_buffer_peek_memory(b,0);auto*ahb=gst_framelink_memory_get_hardware_buffer(mem);CHECK(ahb);
 CHECK(!gst_memory_share(mem,0,16));CHECK(!gst_memory_copy(mem,0,-1));
 GstMapInfo forbidden{};CHECK(!gst_memory_map(mem,&forbidden,GST_MAP_WRITE));
 AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(ahb,&desc);void*data=nullptr;
 CHECK(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data)==0);
 for(unsigned y=0;y<32;++y)memset((char*)data+y*desc.stride*4,71,256);
 CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0); // source-content generation; sink does not map/copy
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
  using GetId=int(*)(const AHardwareBuffer*,uint64_t*);auto getId=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");CHECK(getId);
  uint64_t a=0,z=0;CHECK(getId(ahb,&a)==0&&getId(flImageAHardwareBuffer(frame.image),&z)==0&&a==z);
  flFrameMetadata metadata{};CHECK(flGetFrameMetadata(c,&frame,&metadata)==FL_OK&&metadata.dtsNs==900&&metadata.durationNs==12345);
  flMappedImage map{};CHECK(flImageMapPlanes(frame.image,&map)==FL_OK);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(((unsigned char*)map.plane[0].pixels)[y*map.plane[0].stride+x]==71);
  flImageUnmap(frame.image);CHECK(flRelease(c,&frame)==FL_OK);
  g_signal_emit_by_name(sink,"acquire-native-buffer",0u,&blocked);CHECK(!blocked);
 }
 close(ready);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(in);gst_object_unref(sink);gst_object_unref(p);
 CHECK(gst_framelink_memory_get_hardware_buffer(gst_buffer_peek_memory(b,0))==ahb);gst_buffer_unref(b);gst_buffer_unref(other);flClose(c);
 printf("%s native AHB sink %s: bounded original allocations, explicit readiness, metadata and retained shutdown pass\n",format.c_str(),missingReady?"missing-ready negative":"roundtrip");
}
int main(int argc,char**argv){gst_init(&argc,&argv);GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);run(false);run(true);run(false,true);}
