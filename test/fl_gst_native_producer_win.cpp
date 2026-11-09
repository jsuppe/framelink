#include "../gst/fl_gst_d3d11_producer.hpp"
#include "../gst/framelink_session_context.h"
#include <vector>
#include <thread>
#include <chrono>
#include <gst/app/gstappsrc.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <psapi.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "gst_memory_policy_fixture.h"
static void run(bool nv12){
 auto name="native-producer-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr,*p=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,nv12?FL_FORMAT_NV12:FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&c)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);auto owner=std::make_shared<FrameLinkD3DProducer>(p);CHECK(owner->device);
 auto*b=owner->acquire(1000);auto*other=owner->acquire(1000);CHECK(b&&other&&!owner->acquire(0));
 auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_d3d11_memory(mem));
 auto*reservation=(FrameLinkD3DReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(mem),FrameLinkD3DProducer::key());CHECK(reservation);
 CHECK(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem))==(ID3D11Resource*)flImageD3D11(reservation->buffer.image));
 // Synthetic producer GPU upload creates test content. No sink boundary copy.
 std::vector<unsigned char> pixels(64*(nv12?48:128),71);if(nv12)std::fill(pixels.begin()+64*32,pixels.end(),72);
 ID3D11DeviceContext*ctx=nullptr;flChannelD3D11Device(p)->GetImmediateContext(&ctx);
 gst_d3d11_device_lock(owner->device);ctx->UpdateSubresource(flImageD3D11(reservation->buffer.image),0,nullptr,pixels.data(),nv12?64:256,0);gst_d3d11_device_unlock(owner->device);ctx->Release();
 flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,123,987654,7,{}};
 GST_MINI_OBJECT_FLAG_SET(mem,GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);
 CHECK(owner->submit(b,1000,&meta)==FL_FORMAT_UNSUPPORTED);
 GST_MINI_OBJECT_FLAG_UNSET(mem,GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);
 auto savedSize=mem->size;mem->size-=4;CHECK(owner->submit(b,1000,&meta)==FL_INVALID);mem->size=savedSize;

 CHECK(owner->submit(b,1000,&meta)==FL_OK);CHECK(owner->submit(b,1000,&meta)==FL_INVALID);
 flFrame frame{};CHECK(flAcquireFrame(c,&frame,1000)==FL_OK&&frame.ptsNs==1000);
 flFrameMetadata got{};CHECK(flGetFrameMetadata(c,&frame,&got)==FL_OK&&got.durationNs==987654&&got.streamGeneration==7);
 flMappedImage mapped{};CHECK(flImageMapPlanes(frame.image,&mapped)==FL_OK);
 for(unsigned plane=0;plane<mapped.planes;++plane)for(unsigned y=0;y<mapped.plane[plane].rows;++y)for(unsigned x=0;x<(nv12?64u:256u);++x)
  CHECK(((unsigned char*)mapped.plane[plane].pixels)[y*mapped.plane[plane].stride+x]==71+plane);
 flImageUnmap(frame.image);CHECK(flRelease(c,&frame)==FL_OK);
 CHECK(!owner->acquire(10)); // producer GstMemory still retained despite remote completion
 auto*retained=gst_memory_ref(mem);gst_buffer_unref(b);CHECK(!owner->acquire(10));
 auto before=g_get_monotonic_time();std::thread finalizer([&]{g_usleep(20000);gst_memory_unref(retained);});
 auto*reused=owner->acquire(1000);finalizer.join();CHECK(reused&&g_get_monotonic_time()-before<500000);
 // A copied/system allocation is not accepted, even with matching dimensions.
 auto*foreign=gst_buffer_new_allocate(nullptr,pixels.size(),nullptr);CHECK(owner->submit(foreign,1,&meta)==FL_INVALID);gst_buffer_unref(foreign);
 gst_buffer_unref(reused); // unwritten reservation cancels safely
 auto*last=gst_buffer_peek_memory(other,0);gst_memory_ref(last);gst_buffer_unref(other);
 std::weak_ptr<FrameLinkD3DProducer> weak=owner;owner.reset();CHECK(!weak.expired());
 gst_memory_unref(last);CHECK(weak.expired());flClose(c);
 printf("%s native producer: original texture, ready fence, exact pixels/metadata, local reference reuse and owner lifetime pass\n",nv12?"NV12":"RGBA");
}
// Cached wrappers must still download fresh pixels and describe a resized ring.
static void producerLayoutReuse(bool nv12){
 auto name="native-producer-layout-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr,*p=nullptr;
 const auto format=nv12?FL_FORMAT_NV12:FL_FORMAT_RGBA8;
 CHECK(flCreateChannelEx(name.c_str(),64,32,format,2,FL_MAP_CPU,&c)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);auto owner=std::make_shared<FrameLinkD3DProducer>(p);CHECK(owner->valid());
 for(unsigned step=0;step<3;++step){
  const unsigned width=step==1?640:64,height=32,pitch=width*(nv12?1:4);
  if(step){CHECK(flRequestGeometry(p,width,height,format)==FL_OK);flChannelInfo info{};CHECK(flQuery(p,&info)==FL_OK&&info.width==width);}
  for(unsigned n=0;n<32;++n){
   auto*b=owner->acquire(1000);CHECK(b);auto*memory=gst_buffer_peek_memory(b,0);
   auto*meta=gst_buffer_get_video_meta(b);CHECK(meta&&meta->width==width&&meta->height==height&&meta->n_planes==(nv12?2u:1u));
   auto*texture=(ID3D11Texture2D*)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(memory));
   const auto value=static_cast<unsigned char>(31+step*32+n);
   std::vector<unsigned char> pixels(pitch*(nv12?height*3/2:height),value);
   ID3D11DeviceContext*context=nullptr;flChannelD3D11Device(p)->GetImmediateContext(&context);
   gst_d3d11_device_lock(owner->device);context->UpdateSubresource(texture,0,nullptr,pixels.data(),pitch,0);gst_d3d11_device_unlock(owner->device);context->Release();
   // Readback is the explicit test oracle, not a producer transport operation.
   GstMapInfo map{};CHECK(gst_memory_map(memory,&map,GST_MAP_READ));
   for(unsigned plane=0;plane<meta->n_planes;++plane){const unsigned rows=plane?height/2:height;
    CHECK(meta->stride[plane]>=gint(pitch)&&map.size>=meta->offset[plane]+gsize(rows-1)*meta->stride[plane]+pitch);
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<pitch;++x)CHECK(map.data[meta->offset[plane]+y*meta->stride[plane]+x]==value);
   }
   gst_memory_unmap(memory,&map);gst_buffer_unref(b); // cancel only after the GPU readback completes
  }
 }
 owner.reset();flClose(c);
 puts("native producer layout: repeated fresh pixels, exact planes, grow/shrink and cancelled reservations pass");
}
static FlGstSession* bindSession(GstElement* sink,guint64 generation){
 auto* session=fl_gst_session_new(FL_GST_TRACK_VIDEO);
 flSessionConfig config{};config.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 config.sessionId.bytes[0]=1;config.clockId.bytes[0]=2;config.generation=generation;
 config.epochNs=37000000;config.connectTimeoutMs=100;config.idleTimeoutMs=100;
 config.videoTrackId=1;config.video={FL_FORMAT_RGBA8,64,32,30,1};config.videoQueueFrames=1;
 flSessionCapabilities offer{};offer.version=config.version;offer.videoCount=1;offer.video[0]=config.video;
 CHECK(fl_gst_session_configure(session,&config,&offer,&offer)==FL_GST_SESSION_READY);
 auto* context=fl_gst_session_create_context(session);gst_element_set_context(sink,context);gst_context_unref(context);
 return session;
}
static void coordinatedPolicyNegative(bool stale,bool bin=false){
 auto name="native-policy-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_REQUIRE_FRAME_METADATA,&c)==FL_OK);
 auto*sink=gst_element_factory_make(bin?"framelinkavsink":"framelinksink",nullptr);CHECK(sink);
 if(bin)g_object_set(sink,"tracks","video","native-memory",TRUE,nullptr);
 else g_object_set(sink,"native-memory",TRUE,"sync",FALSE,"async",FALSE,nullptr);
 if(stale)testBindNativePolicy(sink,name,FL_MEMORY_D3D11);
 else {flChannel*p=nullptr;CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);gboolean adopted=FALSE;g_signal_emit_by_name(sink,"adopt-channel",p,&adopted);CHECK(adopted);}
 auto*session=bindSession(sink,stale?2:1);
 CHECK(gst_element_set_state(sink,GST_STATE_PAUSED)==GST_STATE_CHANGE_FAILURE);
 gst_element_set_state(sink,GST_STATE_NULL);gst_object_unref(sink);g_object_unref(session);flClose(c);
 puts(stale?"coordinated native stale policy rejected":"coordinated native missing policy rejected");
}
static void binConfigurationNegatives(){
 for(int mode=0;mode<4;++mode){
  auto*sink=gst_element_factory_make("framelinkavsink",nullptr);CHECK(sink);
  g_object_set(sink,"native-memory",TRUE,nullptr);
  if(mode==1)g_object_set(sink,"tracks","audio",nullptr);
  if(mode==2)g_object_set(sink,"shared-cpu",TRUE,nullptr);
  if(mode==3)g_object_set(sink,"session","native-must-not-discover",nullptr);
  CHECK(gst_element_set_state(sink,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);
  gst_element_set_state(sink,GST_STATE_NULL);gst_object_unref(sink);
 }
 puts("native bin rejects missing policy, audio-only, shared CPU and automatic discovery");
}
static void sinkPipeline(bool coordinated,bool bin=false){
 auto name="native-sink-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&c)==FL_OK);
 GError* error=nullptr;
 auto*pipeline=gst_parse_launch(("appsrc name=in is-live=true format=time caps=video/x-raw(memory:D3D11Memory),format=RGBA,width=64,height=32,framerate=30/1 ! "+(bin?std::string("framelinkavsink name=sink tracks=video native-memory=true"):"framelinksink name=sink native-memory=true preserve-frame-metadata=true channel="+name+" sync=false async=false enable-last-sample=false")).c_str(),&error);CHECK(pipeline&&!error);
 auto*sink=gst_bin_get_by_name(GST_BIN(pipeline),"sink");auto*in=gst_bin_get_by_name(GST_BIN(pipeline),"in");
 if(bin){auto*child=gst_bin_get_by_name(GST_BIN(sink),"video");g_object_set(child,"sync",FALSE,"async",FALSE,"enable-last-sample",FALSE,nullptr);gst_object_unref(child);}
 testBindNativePolicy(sink,name,FL_MEMORY_D3D11);
 auto*session=coordinated?bindSession(sink,1):nullptr;
 CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*caps=gst_caps_from_string("video/x-raw(memory:D3D11Memory),format=RGBA,width=64,height=32,framerate=30/1");
 auto*pad=gst_element_get_static_pad(sink,bin?"video_sink":"sink");auto*query=gst_query_new_allocation(caps,TRUE);testQueryMemoryRequirements(query,true);
 CHECK(!gst_pad_query(pad,query)); // unaware upstream cannot select unsafe completion semantics
 gst_structure_set(gst_query_writable_structure(query),"framelink-completion-contract",G_TYPE_STRING,"future-unknown-version",nullptr);
 CHECK(!gst_pad_query(pad,query));
 auto*wrongCaps=gst_caps_copy(caps);gst_caps_set_simple(wrongCaps,"width",G_TYPE_INT,128,nullptr);
 auto*wrongQuery=gst_query_new_allocation(wrongCaps,TRUE);testQueryMemoryRequirements(wrongQuery,true);
 gst_structure_set(gst_query_writable_structure(wrongQuery),"framelink-completion-contract",G_TYPE_STRING,"same-device-before-abandon-v1",nullptr);
 CHECK(!gst_pad_query(pad,wrongQuery));gst_query_unref(wrongQuery);gst_caps_unref(wrongCaps);
 gst_structure_set(gst_query_writable_structure(query),"framelink-completion-contract",G_TYPE_STRING,"same-device-before-abandon-v1",nullptr);
 testPolicyQueryNegatives(pad,query);
 CHECK(gst_pad_query(pad,query)&&gst_query_get_n_allocation_pools(query)==1);
 GstBufferPool*pool=nullptr;guint size=0,min=9,max=9;gst_query_parse_nth_allocation_pool(query,0,&pool,&size,&min,&max);CHECK(pool&&min==0&&max==2);
 gst_query_unref(query);gst_caps_unref(caps);gst_object_unref(pad);CHECK(gst_buffer_pool_set_active(pool,TRUE));
 GstBuffer*b=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK&&b);
 GstBuffer*spare=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&spare,nullptr)==GST_FLOW_OK);
 GstBuffer*blocked=nullptr;GstBufferPoolAcquireParams params{};params.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
 CHECK(gst_buffer_pool_acquire_buffer(pool,&blocked,&params)==GST_FLOW_EOS);
 gst_buffer_unref(spare);CHECK(gst_buffer_pool_acquire_buffer(pool,&spare,nullptr)==GST_FLOW_OK);gst_buffer_unref(spare);

 auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_d3d11_memory(mem));
 auto*texture=(ID3D11Texture2D*)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem));
 ID3D11Device*dev=nullptr;texture->GetDevice(&dev);ID3D11DeviceContext*ctx=nullptr;dev->GetImmediateContext(&ctx);
 std::vector<unsigned char> pixels(64*32*4,91);ctx->UpdateSubresource(texture,0,nullptr,pixels.data(),256,0);ctx->Release();dev->Release();
 GST_BUFFER_PTS(b)=1000;GST_BUFFER_DTS(b)=900;GST_BUFFER_DURATION(b)=12345;
 CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),gst_buffer_ref(b))==GST_FLOW_OK);
 flFrame frame{};CHECK(flAcquireFrame(c,&frame,2000)==FL_OK&&frame.ptsNs==1000);
 flFrameMetadata meta{};CHECK(flGetFrameMetadata(c,&frame,&meta)==FL_OK&&meta.dtsNs==900&&meta.durationNs==12345);
 if(coordinated)CHECK(meta.streamGeneration==1);
 flMappedImage map{};CHECK(flImageMapPlanes(frame.image,&map)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(((unsigned char*)map.plane[0].pixels)[y*map.plane[0].stride+x]==91);
 flImageUnmap(frame.image);CHECK(flRelease(c,&frame)==FL_OK);
 auto*foreign=gst_buffer_new_allocate(nullptr,pixels.size(),nullptr);GST_BUFFER_PTS(foreign)=2000;
 CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),foreign)==GST_FLOW_OK);
 auto*bus=gst_element_get_bus(pipeline);auto*failure=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GST_MESSAGE_ERROR);CHECK(failure);gst_message_unref(failure);gst_object_unref(bus);
 CHECK(flAcquireFrame(c,&frame,20)==FL_TIMEOUT);
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(in);gst_object_unref(sink);gst_object_unref(pipeline);
 CHECK(gst_buffer_pool_acquire_buffer(pool,&blocked,&params)==GST_FLOW_FLUSHING);
 CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
 CHECK(gst_is_d3d11_memory(gst_buffer_peek_memory(b,0)));gst_buffer_unref(b);flClose(c);if(session)g_object_unref(session);
 puts("native sink pipeline: exact texture pixels/metadata, foreign allocation rejection, retained buffer after stop pass");
}
#include "fl_gst_native_producer_av_win.inc"
#include "fl_gst_decoder_contract_win.inc"
#include "fl_gst_native_relay_av_win.inc"
int main(int argc,char**argv){gst_init(&argc,&argv);CHECK(argc==2||argc==3);GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
 if(argc==3){
  if(std::string(argv[2])=="--relay-av-diagnostic"){nativeRelayAv(false);nativeRelayAv(true);for(unsigned fault=1;fault<=12;++fault)nativeRelayAv(false,fault);return 0;}
  if(std::string(argv[2])=="--relay-contract-diagnostic"){
   for(unsigned mode=0;mode<2;++mode)nativeProducerAv(mode,true,false,true,true,false,true);
   return 0;
  }
  if(std::string(argv[2])=="--decoder-contract-diagnostic"){decoderContractDiagnostic();return 0;}
  if(std::string(argv[2])=="--decoder-av-diagnostic"){
   for(bool nativeReceiver:{false,true})for(unsigned mode=0;mode<2;++mode)
    nativeProducerAv(mode,true,false,true,nativeReceiver,true);
   return 0;
  }
  CHECK(std::string(argv[2])=="--lifecycle-diagnostic");
  auto memoryInfo=reinterpret_cast<BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));CHECK(memoryInfo);
  // Explicit diagnostic: no acceptance thresholds are changed or waived.
  for(unsigned cycle=0;cycle<100;++cycle){
   nativeProducerAv(cycle%3,true,false,true);gst_task_cleanup_all();
   PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);DWORD handles=0;
   CHECK(memoryInfo(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory),sizeof(memory)));
   CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles));
   printf("LIFECYCLE,%u,%llu,%lu\n",cycle,static_cast<unsigned long long>(memory.PrivateUsage),static_cast<unsigned long>(handles));fflush(stdout);
  }
  return 0;
 }
 run(false);run(true);producerLayoutReuse(false);producerLayoutReuse(true);sinkPipeline(false);coordinatedPolicyNegative(false);coordinatedPolicyNegative(true);sinkPipeline(true);sinkPipeline(true,true);binConfigurationNegatives();coordinatedPolicyNegative(false,true);coordinatedPolicyNegative(true,true);for(unsigned mode=0;mode<4;++mode)nativeProducerAv(mode);for(unsigned mode=0;mode<4;++mode)nativeProducerAv(mode,false,false,false,true);for(unsigned mode=0;mode<3;++mode){nativeProducerAv(mode,true,false,false,true);nativeProducerAv(mode,true,true,false,true);nativeProducerAv(mode,true,false,true,true);}for(unsigned mode=0;mode<3;++mode)nativeProducerAv(mode,true);for(unsigned mode=0;mode<3;++mode)nativeProducerAv(mode,true,true);nativeLocalNegatives();nativeLocalNegatives(true);nativeBinNegatives();nativeBinNegatives(true);for(unsigned mode=0;mode<3;++mode)nativeProducerAv(mode,true,false,true);}
