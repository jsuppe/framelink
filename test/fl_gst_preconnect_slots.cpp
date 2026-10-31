#include "framelink.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <cstdio>
#include <string>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
 gst_init(&argc,&argv);CHECK(argc==2);GError* error=nullptr;
 auto* plugin=gst_plugin_load_file(argv[1],&error);CHECK(plugin);gst_object_unref(plugin);
 const std::string prefix="preconnect-slots-"+std::to_string(g_get_monotonic_time());
 const std::string channel=prefix+".0";
 auto* output=gst_element_factory_make("framelinksink",nullptr);CHECK(output);
 gst_object_ref_sink(output);
 g_object_set(output,"sync",FALSE,"async",FALSE,"slots",1,"connect-timeout-ms",1000u,"channel",prefix.c_str(),nullptr);
 // Same element instance across NULL/PLAYING. First caps become deliberately
 // stale on second start; preconnect must not request those old dimensions.
 for(unsigned iteration=0;iteration<2;++iteration){
  const unsigned width=iteration?96:64,height=iteration?64:48;
  flChannel* consumer=nullptr;
  CHECK(flCreateChannelEx(channel.c_str(),width,height,FL_FORMAT_NV12,3,FL_MAP_CPU,&consumer)==FL_OK);
  auto* pipeline=gst_pipeline_new(nullptr);auto* input=gst_element_factory_make("appsrc",nullptr);CHECK(pipeline && input);
  g_object_set(input,"is-live",TRUE,"format",GST_FORMAT_TIME,nullptr);
  gst_bin_add_many(GST_BIN(pipeline),input,GST_ELEMENT(gst_object_ref(output)),nullptr);
  CHECK(gst_element_link(input,output));
  CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  flChannelInfo info{};CHECK(flQuery(consumer,&info)==FL_OK);
  CHECK(info.width==width && info.height==height && info.format==FL_FORMAT_NV12);
  flChannel* duplicate=nullptr;CHECK(flOpenProducer(channel.c_str(),&duplicate)==FL_BUSY && !duplicate);
  auto* caps=gst_caps_new_simple("video/x-raw","format",G_TYPE_STRING,"NV12","width",G_TYPE_INT,int(width),"height",G_TYPE_INT,int(height),"framerate",GST_TYPE_FRACTION,30,1,nullptr);
  gst_app_src_set_caps(GST_APP_SRC(input),caps);gst_caps_unref(caps);
  auto* buffer=gst_buffer_new_allocate(nullptr,width*height*3/2,nullptr);CHECK(buffer);
  GST_BUFFER_PTS(buffer)=0;GST_BUFFER_DURATION(buffer)=GST_SECOND/30;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(input),buffer)==GST_FLOW_OK);
  flFrame frame{};CHECK(flAcquireFrame(consumer,&frame,3000)==FL_OK);CHECK(flRelease(consumer,&frame)==FL_OK);
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  gst_object_unref(pipeline);
  flClose(consumer);
 }
 gst_object_unref(output);
 puts("geometry-independent slot claim and fresh caps on restart passed");return 0;
}
