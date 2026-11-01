#pragma once
#include "../gst/framelink_memory_policy.h"
static void testBindNativePolicy(GstElement*sink,const std::string&name,uint32_t resource){
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);gboolean accepted=FALSE;
 g_signal_emit_by_name(sink,"adopt-channel",producer,&accepted);CHECK(accepted);
 GstFrameLinkMemoryPolicy policy{};policy.version=1;policy.sessionGeneration=1;
 for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);policy.requirements[i].allowedResourceTypes=resource;policy.paths[i].version=1;policy.paths[i].resourceType=resource;policy.paths[i].supportedUsage=FL_MEMORY_GPU_SAMPLE;}
 policy.requirements[0].requiredUsage=FL_MEMORY_GPU_RENDER;
 g_signal_emit_by_name(sink,"apply-memory-policy",&policy,&accepted);CHECK(!accepted);
 policy.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE;
 g_signal_emit_by_name(sink,"apply-memory-policy",&policy,&accepted);CHECK(accepted);
 g_signal_emit_by_name(sink,"apply-memory-policy",&policy,&accepted);CHECK(!accepted);
 g_object_set(sink,"native-memory",FALSE,nullptr);gboolean native=FALSE;g_object_get(sink,"native-memory",&native,nullptr);CHECK(native);
}
static void testQueryMemoryRequirements(GstQuery*query,bool supported){
 flMemoryRequirements req{};flMemoryRequirementsInit(&req);req.requiredUsage=supported?FL_MEMORY_GPU_SAMPLE:FL_MEMORY_GPU_RENDER;
 unsigned char wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&req,wire,sizeof(wire))==FL_MEMORY_OK);
 auto*bytes=g_bytes_new(wire,sizeof(wire));gst_structure_set(gst_query_writable_structure(query),"framelink-memory-requirements",G_TYPE_BYTES,bytes,nullptr);g_bytes_unref(bytes);
}
static void testPolicyQueryNegatives(GstPad*pad,GstQuery*query){
 gst_structure_remove_field(gst_query_writable_structure(query),"framelink-memory-requirements");CHECK(!gst_pad_query(pad,query));
 testQueryMemoryRequirements(query,false);CHECK(!gst_pad_query(pad,query));testQueryMemoryRequirements(query,true);
}
