#include "framelink.h"
#include "../gst/framelink_memory_policy.h"
#include <gst/app/gstappsink.h>
#include <gst/base/gstbasesink.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void publish(flChannel*p,int64_t pts){flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);CHECK(flSubmit(p,&b,pts)==FL_OK);}
static void run(bool bound,bool beforeStart=false){auto name="policy-generation-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr,*p=nullptr;CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 GError*e=nullptr;auto*pipeline=gst_parse_launch("framelinksrc name=source width=64 height=32 format=RGBA preserve-timestamps=true ! appsink name=out sync=false",&e);CHECK(pipeline&&!e);auto*source=gst_bin_get_by_name(GST_BIN(pipeline),"source");auto*out=gst_bin_get_by_name(GST_BIN(pipeline),"out");gboolean accepted=FALSE;g_signal_emit_by_name(source,"adopt-channel",c,&accepted);CHECK(accepted);
 if(bound){GstFrameLinkMemoryPolicy policy{};policy.version=1;policy.sessionGeneration=1;for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);policy.requirements[i].allowedOperations=FL_MEMORY_OPERATIONS_ALL;policy.paths[i].version=1;policy.paths[i].operations=FL_MEMORY_OPERATIONS_ALL;
#ifdef _WIN32
 policy.paths[i].resourceType=FL_MEMORY_D3D11;
#elif defined(__ANDROID__)
 policy.paths[i].resourceType=FL_MEMORY_AHB;
#else
 policy.paths[i].resourceType=FL_MEMORY_DMABUF;
#endif
 }g_signal_emit_by_name(source,"apply-memory-policy",&policy,&accepted);CHECK(accepted);g_object_set(source,"width",128,nullptr);gint width=0;g_object_get(source,"width",&width,nullptr);CHECK(width==64);}
 if(beforeStart){
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE);
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(source);gst_object_unref(out);gst_object_unref(pipeline);flClose(p);
 printf("authenticated pool changed before start: rejected\n");return;
 }
 CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);publish(p,1000);auto*first=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(first);gst_sample_unref(first);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);flChannelInfo info{};CHECK(flQuery(p,&info)==FL_OK&&info.width==128);publish(p,2000);
 auto*second=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);
 if(bound){CHECK(!second);auto*bus=gst_element_get_bus(pipeline);auto*message=gst_bus_timed_pop_filtered(bus,GST_SECOND,GST_MESSAGE_ERROR);CHECK(message);GError*error=nullptr;gchar*debug=nullptr;gst_message_parse_error(message,&error,&debug);CHECK(error&&std::string(error->message).find("generation changed")!=std::string::npos);g_error_free(error);g_free(debug);gst_message_unref(message);gst_object_unref(bus);}
 else {CHECK(second);auto*caps=gst_sample_get_caps(second);gint width=0;CHECK(gst_structure_get_int(gst_caps_get_structure(caps,0),"width",&width)&&width==128);gst_sample_unref(second);}
 gst_element_set_state(pipeline,GST_STATE_NULL);if(bound){g_signal_emit_by_name(source,"apply-memory-policy",nullptr,&accepted);CHECK(!accepted);}gst_object_unref(source);gst_object_unref(out);gst_object_unref(pipeline);flClose(p);printf("%s pool generation: %s\n",bound?"authenticated":"legacy",bound?"fails closed before new-pool frame":"renegotiates as before");
}
static void sinkGeneration(bool beforeStart){
 auto name="sink-policy-generation-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr,*p=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 auto*sink=gst_element_factory_make("framelinksink",nullptr);CHECK(sink);g_object_set(sink,"async",FALSE,"sync",FALSE,nullptr);
 gboolean accepted=FALSE;g_signal_emit_by_name(sink,"adopt-channel",p,&accepted);CHECK(accepted);
 GstFrameLinkMemoryPolicy policy{};policy.version=1;policy.sessionGeneration=1;
 for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);policy.requirements[i].allowedOperations=FL_MEMORY_OPERATIONS_ALL;policy.paths[i].version=1;policy.paths[i].operations=FL_MEMORY_OPERATIONS_ALL;
#ifdef _WIN32
 policy.paths[i].resourceType=FL_MEMORY_D3D11;
#elif defined(__ANDROID__)
 policy.paths[i].resourceType=FL_MEMORY_AHB;
#else
 policy.paths[i].resourceType=FL_MEMORY_DMABUF;
#endif
 }
 g_signal_emit_by_name(sink,"apply-memory-policy",&policy,&accepted);CHECK(accepted);
 if(!beforeStart)CHECK(gst_element_set_state(sink,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 if(beforeStart)CHECK(gst_element_set_state(sink,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE);
 else {auto*buffer=gst_buffer_new();CHECK(GST_BASE_SINK_GET_CLASS(sink)->render(GST_BASE_SINK(sink),buffer)==GST_FLOW_NOT_NEGOTIATED);gst_buffer_unref(buffer);}
 gst_element_set_state(sink,GST_STATE_NULL);gst_object_unref(sink);flClose(c);
 printf("authenticated sink generation rejected %s\n",beforeStart?"before start":"before submit");
}
static gboolean readerAllocation(GstElement*,GstQuery*query,gpointer){
 flMemoryRequirements requirements{};flMemoryRequirementsInit(&requirements);requirements.requiredUsage=FL_MEMORY_GPU_SAMPLE;
 unsigned char wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&requirements,wire,sizeof(wire))==FL_MEMORY_OK);auto*bytes=g_bytes_new(wire,sizeof(wire));
 gst_structure_set(gst_query_writable_structure(query),"framelink-reader-completion-contract",G_TYPE_STRING,"retain-memory-until-gpu-complete-v1","framelink-memory-requirements",G_TYPE_BYTES,bytes,nullptr);g_bytes_unref(bytes);return TRUE;
}
static void readerGate(bool agree){auto name="reader-policy-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr,*p=nullptr;CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 GError*e=nullptr;auto*pipeline=gst_parse_launch("framelinksrc name=source width=64 height=32 format=RGBA native-memory=true preserve-timestamps=true ! appsink name=out sync=false async=false enable-last-sample=false",&e);CHECK(pipeline&&!e);auto*source=gst_bin_get_by_name(GST_BIN(pipeline),"source");auto*out=gst_bin_get_by_name(GST_BIN(pipeline),"out");gboolean accepted=FALSE;g_signal_emit_by_name(source,"adopt-channel",c,&accepted);CHECK(accepted);
 GstFrameLinkMemoryPolicy policy{};policy.version=1;policy.sessionGeneration=1;for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);policy.paths[i].version=1;policy.paths[i].supportedUsage=FL_MEMORY_GPU_SAMPLE;
#ifdef _WIN32
 policy.paths[i].resourceType=FL_MEMORY_D3D11;
#elif defined(__ANDROID__)
 policy.paths[i].resourceType=FL_MEMORY_AHB;
#else
 policy.paths[i].resourceType=FL_MEMORY_DMABUF;
#endif
 }
 g_signal_emit_by_name(source,"apply-memory-policy",&policy,&accepted);CHECK(accepted);
 if(agree){CHECK(g_signal_lookup("propose-allocation",G_OBJECT_TYPE(out)));g_object_set(out,"emit-signals",TRUE,nullptr);g_signal_connect(out,"propose-allocation",G_CALLBACK(readerAllocation),nullptr);}
 CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);publish(p,1000);auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(bool(sample)==agree);if(sample){CHECK(GST_BUFFER_PTS(gst_sample_get_buffer(sample))==1000);gst_sample_unref(sample);}
 else {auto*bus=gst_element_get_bus(pipeline);auto*message=gst_bus_timed_pop_filtered(bus,GST_SECOND,GST_MESSAGE_ERROR);CHECK(message);gst_message_unref(message);gst_object_unref(bus);}
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(source);gst_object_unref(out);gst_object_unref(pipeline);flClose(p);printf("native reader policy %s\n",agree?"explicit completion/usage accepted":"missing contract rejected before delivery");
}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
#else
 CHECK(argc==2);GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
#endif
 run(false);run(true);run(true,true);sinkGeneration(true);sinkGeneration(false);readerGate(false);readerGate(true);}
