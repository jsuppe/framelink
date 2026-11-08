#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include "framelink.h"
#include "../gst/framelink_memory_policy.h"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static GstFrameLinkMemoryPolicy policy(){
 GstFrameLinkMemoryPolicy p{};p.version=1;p.sessionGeneration=1;
 for(unsigned i=0;i<2;i++){CHECK(flMemoryRequirementsInit(&p.requirements[i])==FL_MEMORY_OK);p.requirements[i].allowedResourceTypes=FL_MEMORY_DMABUF;p.requirements[i].allowedOperations=FL_MEMORY_COPY|FL_MEMORY_READBACK;p.paths[i].version=1;p.paths[i].resourceType=FL_MEMORY_DMABUF;}
 p.paths[0].operations=FL_MEMORY_COPY|FL_MEMORY_READBACK;return p;
}
static unsigned char value(unsigned n,unsigned x,unsigned y,unsigned c){return (n*17+x*3+y*7+c*59)%256;}
static void verify(GstSample*sample,unsigned n,unsigned w,unsigned h,const char*fmt){
 CHECK(sample);auto*b=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(b)==n*16666667ULL);
 GstVideoInfo info{};CHECK(gst_video_info_from_caps(&info,gst_sample_get_caps(sample)));CHECK(info.width==(int)w&&info.height==(int)h&&GST_VIDEO_INFO_FORMAT(&info)==gst_video_format_from_string(fmt));
 GstVideoFrame f{};CHECK(gst_video_frame_map(&f,&info,b,GST_MAP_READ));
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<4;c++)CHECK(((unsigned char*)GST_VIDEO_FRAME_PLANE_DATA(&f,0))[y*GST_VIDEO_FRAME_PLANE_STRIDE(&f,0)+x*4+c]==value(n,x,y,c));
 gst_video_frame_unmap(&f);
}
int main(int argc,char**argv){
 gst_init(&argc,&argv);CHECK(argc==2);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);CHECK(plugin&&!error);gst_object_unref(plugin);
 // An opt-in flag alone cannot authorize hidden copies/readback.
 auto*unbound=gst_element_factory_make("framelinksrc",nullptr);CHECK(unbound);g_object_set(unbound,"gpu-readback",TRUE,nullptr);CHECK(gst_element_set_state(unbound,GST_STATE_PAUSED)==GST_STATE_CHANGE_FAILURE);gst_element_set_state(unbound,GST_STATE_NULL);gst_object_unref(unbound);
 for(const char*fmt:{"BGRA","RGBA"})for(unsigned w:{1920u,3840u}){
  unsigned h=w==1920?1080:2160;auto name="gst-egl-readback-"+std::to_string(getpid())+fmt+std::to_string(w);
  std::string desc="framelinksrc name=src channel="+name+" gpu-readback=true preserve-timestamps=true format="+fmt+" width="+std::to_string(w)+" height="+std::to_string(h)+" ! appsink name=out sync=false";
  auto*pipeline=gst_parse_launch(desc.c_str(),&error);CHECK(pipeline&&!error);
  auto*source=gst_bin_get_by_name(GST_BIN(pipeline),"src");auto*out=gst_bin_get_by_name(GST_BIN(pipeline),"out");
  flChannel*c=nullptr,*producer=nullptr;CHECK(flCreateChannelEx(name.c_str(),w,h,!strcmp(fmt,"BGRA")?FL_FORMAT_BGRA8:FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c)==FL_OK);
  CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
  gboolean accepted=FALSE;g_signal_emit_by_name(source,"adopt-channel",c,&accepted);CHECK(accepted);
  auto p=policy();for(unsigned endpoint=0;endpoint<2;endpoint++){auto denied=p;denied.requirements[endpoint].allowedOperations=FL_MEMORY_COPY;g_signal_emit_by_name(source,"apply-memory-policy",&denied,&accepted);CHECK(!accepted);}
  auto omitted=p;omitted.paths[0].operations=FL_MEMORY_COPY;g_signal_emit_by_name(source,"apply-memory-policy",&omitted,&accepted);CHECK(!accepted);
  g_signal_emit_by_name(source,"apply-memory-policy",&p,&accepted);CHECK(accepted);
  g_object_set(source,"gpu-readback",FALSE,nullptr);gboolean enabled=FALSE;g_object_get(source,"gpu-readback",&enabled,nullptr);CHECK(enabled);
  CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  GstSample*held=nullptr;std::vector<unsigned char>row(w*4);
  for(unsigned n=0;n<8;n++){
   flBuffer b{};CHECK(flAcquireBuffer(producer,&b,2000)==FL_OK);flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);
   for(unsigned y=0;y<h;y++){for(unsigned x=0;x<w;x++)for(unsigned k=0;k<4;k++)row[x*4+k]=value(n,x,y,k);memcpy((char*)m.plane[0].pixels+y*m.plane[0].stride,row.data(),row.size());}
   flImageUnmap(b.image);CHECK(flSubmit(producer,&b,n*16666667ULL)==FL_OK);
   auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),5*GST_SECOND);verify(sample,n,w,h,fmt);if(!n)held=gst_sample_ref(sample);gst_sample_unref(sample);
  }
  verify(held,0,w,h,fmt);gst_sample_unref(held);
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);flClose(producer);gst_object_unref(out);gst_object_unref(source);gst_object_unref(pipeline);
 }
 puts("32 GStreamer staging frames: exact channels/alpha/PTS, retained pixels, both-endpoint permission rejection passed");
}
