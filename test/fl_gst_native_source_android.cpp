#include "framelink_ahb.h"
#include "../gst/framelink_native.h"
#include <gst/app/gstappsink.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void fill(flBuffer&b){void*p=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&p,&stride)==FL_OK);for(unsigned y=0;y<32;++y)memset((char*)p+y*stride,71,64*4);flImageUnmap(b.image);}
static void run(const char*format,bool adopt=false){
 GError*e=nullptr;std::string name="gst-ahb-"+std::to_string(g_get_monotonic_time());
 auto*p=gst_parse_launch(("framelinksrc name=source channel="+name+" width=64 height=32 format="+format+" pool-size=2 native-memory=true preserve-timestamps=true ! appsink name=out sync=false async=false enable-last-sample=false").c_str(),&e);CHECK(p&&!e);
 if(adopt){
     auto*src=gst_bin_get_by_name(GST_BIN(p),"source");
     guint w=0,h=0;g_object_get(src,"width",&w,"height",&h,nullptr);
     flFormat fmt=!strcmp(format,"NV12")?FL_FORMAT_NV12:!strcmp(format,"RGB10A2_LE")?FL_FORMAT_RGB10A2:FL_FORMAT_RGBA8;
     flChannel*c=nullptr;gboolean accepted=TRUE;
     CHECK(flCreateChannelEx(name.c_str(),w,h,fmt,2,FL_MAP_CPU,&c)==FL_OK);
     g_signal_emit_by_name(src,"adopt-channel",c,&accepted);CHECK(!accepted);flChannelInfo info{};CHECK(flQuery(c,&info)==FL_OK);flClose(c);
     CHECK(flCreateChannelEx(name.c_str(),w,h,fmt,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
     g_signal_emit_by_name(src,"adopt-channel",c,&accepted);CHECK(accepted);gst_object_unref(src);
 }
 auto*out=gst_bin_get_by_name(GST_BIN(p),"out");CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);flBuffer b{},other{},reserved{},blocked{};
 CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK&&flAcquireBuffer(producer,&other,1000)==FL_OK);fill(b);CHECK(flSubmit(producer,&b,1234567)==FL_OK);
 auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample);auto*buf=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(buf)==1234567);
 CHECK(gst_caps_features_contains(gst_caps_get_features(gst_sample_get_caps(sample),0),"memory:FrameLinkAHardwareBuffer"));CHECK(gst_buffer_n_memory(buf)==1);
 auto*copy=gst_buffer_copy(buf);CHECK(copy&&gst_buffer_peek_memory(copy,0)==gst_buffer_peek_memory(buf,0));gst_buffer_unref(copy);
 auto*mem=gst_buffer_peek_memory(buf,0);auto*ahb=gst_framelink_memory_get_hardware_buffer(mem);CHECK(ahb);
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);auto getId=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");CHECK(getId);uint64_t a=0,c=0;
 CHECK(getId(ahb,&a)==0&&getId(flImageAHardwareBuffer(b.image),&c)==0&&a==c);
 GstMapInfo forbidden{};CHECK(!gst_memory_map(mem,&forbidden,GST_MAP_READ)); // no implicit CPU map/fallback
 auto*retained=gst_memory_ref(mem);gst_sample_unref(sample);CHECK(flAcquireBuffer(producer,&blocked,0)==FL_TIMEOUT);
 auto*preroll=gst_app_sink_try_pull_preroll(GST_APP_SINK(out),0);if(preroll)gst_sample_unref(preroll);gst_memory_unref(retained);
 fill(other);CHECK(flSubmit(producer,&other,2234567)==FL_OK);sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample);
 buf=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(buf)==2234567);retained=gst_memory_ref(gst_buffer_peek_memory(buf,0));gst_sample_unref(sample);
 CHECK(flAcquireBuffer(producer,&reserved,1000)==FL_OK&&reserved.slot==b.slot);CHECK(flAcquireBuffer(producer,&blocked,0)==FL_TIMEOUT);
 CHECK(gst_element_set_state(p,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(out);gst_object_unref(p);
 ahb=gst_framelink_memory_get_hardware_buffer(retained);CHECK(ahb);AHardwareBuffer_Desc d{};AHardwareBuffer_describe(ahb,&d);void*data=nullptr;
 CHECK(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,nullptr,&data)==0);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(((unsigned char*)data)[y*d.stride*4+x]==71);
 CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0);gst_memory_unref(retained);flCancelBuffer(producer,&reserved);flClose(producer);
 printf("%s AHB allocation ID, pixels/PTS, bounded readers and retained memory after stop pass\n",format);
}
int main(int argc,char**argv){gst_init(&argc,&argv);GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);run("RGBA");run("RGB10A2_LE");run("RGBA",true);
 GError*e=nullptr;auto*p=gst_parse_launch("framelinksrc native-memory=true format=NV12 ! fakesink",&e);CHECK(p&&!e);CHECK(gst_element_set_state(p,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);
 puts("opaque Android YUV rejected rather than advertised as native NV12 bytes");}
