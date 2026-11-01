#include "framelink.h"
#include "framelink_dmabuf.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void run(const char* format,bool adopt=false){
 GError*e=nullptr;
 std::string name="native-gst-"+std::to_string(g_get_monotonic_time());
 auto*p=gst_parse_launch(("framelinksrc name=source channel="+name+" width=16 height=16 format="+std::string(format)+" pool-size=2 native-memory=true preserve-timestamps=true ! appsink name=out sync=false async=false enable-last-sample=false").c_str(),&e);CHECK(p&&!e);
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
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 flBuffer b{},other{},excess{};CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK);CHECK(flAcquireBuffer(producer,&other,1000)==FL_OK);
 flDmabuf d{};CHECK(flImageDmabuf(b.image,&d)==FL_OK);struct stat original{};CHECK(!fstat(d.fd[0],&original));
 flMappedImage mapped{};CHECK(flImageMapPlanes(b.image,&mapped)==FL_OK);
 for(unsigned i=0;i<mapped.planes;++i)for(unsigned y=0;y<mapped.plane[i].rows;++y)
     memset((char*)mapped.plane[i].pixels+y*mapped.plane[i].stride,71+i,strcmp(format,"NV12")==0?16:64);
 flImageUnmap(b.image);
 CHECK(flSubmit(producer,&b,1234567)==FL_OK);
 auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample);
 auto*buf=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(buf)==1234567);
 CHECK(gst_caps_features_contains(gst_caps_get_features(gst_sample_get_caps(sample),0),"memory:DMABuf"));
 CHECK(gst_buffer_n_memory(buf)==1);auto*mem=gst_buffer_peek_memory(buf,0);CHECK(gst_is_dmabuf_memory(mem));
 struct stat exported{};CHECK(!fstat(gst_dmabuf_memory_get_fd(mem),&exported));CHECK(exported.st_dev==original.st_dev&&exported.st_ino==original.st_ino);
 auto*meta=gst_buffer_get_video_meta(buf);CHECK(meta&&meta->stride[0]==(gint)mapped.plane[0].stride);
 gsize offsets[4]{};gint strides[4]{};for(unsigned i=0;i<meta->n_planes;++i){offsets[i]=meta->offset[i];strides[i]=meta->stride[i];}
 auto*shared=gst_memory_share(mem,0,-1);CHECK(shared);gst_sample_unref(sample);
 CHECK(flAcquireBuffer(producer,&excess,0)==FL_TIMEOUT);
 auto*preroll=gst_app_sink_try_pull_preroll(GST_APP_SINK(out),0);if(preroll)gst_sample_unref(preroll);
 gst_memory_unref(shared);
 // appsink may retain its current sample until the next pull. Publish the
 // second reserved slot before requiring retirement of the first allocation.
 CHECK(flImageMapPlanes(other.image,&mapped)==FL_OK);
 for(unsigned i=0;i<mapped.planes;++i)for(unsigned y=0;y<mapped.plane[i].rows;++y)
     memset((char*)mapped.plane[i].pixels+y*mapped.plane[i].stride,71+i,strcmp(format,"NV12")==0?16:64);
 flImageUnmap(other.image);
 CHECK(flSubmit(producer,&other,2234567)==FL_OK);
 sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample);
 buf=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(buf)==2234567);
 shared=gst_memory_share(gst_buffer_peek_memory(buf,0),0,-1);CHECK(shared);gst_sample_unref(sample);
 CHECK(flAcquireBuffer(producer,&excess,1000)==FL_OK&&excess.slot==b.slot);
 flBuffer blocked{};CHECK(flAcquireBuffer(producer,&blocked,0)==FL_TIMEOUT);
 // Keep a GstMemory subview alive through element shutdown: allocation and reader
 // lifetime must outlive the source, not merely the original GstBuffer/sample.
 CHECK(gst_element_set_state(p,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(out);gst_object_unref(p);
 GstMapInfo map{};CHECK(gst_memory_map(shared,&map,GST_MAP_READ));
 for(unsigned i=0;i<mapped.planes;++i)for(unsigned y=0;y<mapped.plane[i].rows;++y)
     for(unsigned x=0;x<(strcmp(format,"NV12")==0?16u:64u);++x)CHECK(map.data[offsets[i]+y*strides[i]+x]==71+i);
 gst_memory_unmap(shared,&map);gst_memory_unref(shared);flCancelBuffer(producer,&excess);flClose(producer);
 puts("native DMA-BUF source: original allocation identity, PTS, pixels, bounded ownership and memory-subview lifetime after shutdown pass");
}

int main(int argc,char**argv){
 gst_init(&argc,&argv);CHECK(argc==2);GError*e=nullptr;
 auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
 run("RGBA");run("NV12");run("RGBA",true);
 // Native mode never satisfies a sysmem-only downstream by silently copying.
 auto*p=gst_parse_launch("framelinksrc native-memory=true ! video/x-raw(memory:SystemMemory) ! fakesink",&e);
 CHECK(e);g_error_free(e);if(p)gst_object_unref(p);
 puts("sysmem-only downstream rejected: no silent fallback");
}
