// M6.9 on Linux and Android: native producer allocations (DMA-BUF,
// AHardwareBuffer) in coordinated A/V sessions, selected through
// set-local-native-candidate on both bins. The producer writes into the
// sink's own pool (the ring's allocations); a native receiver must see those
// same allocations, and a CPU-readback receiver the same pixels. PCM,
// PTS/DTS/duration and the A/V offset travel with them. Negatives mirror the
// Windows native A/V controls.
#include "../gst/framelink_session_transport.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <sys/eventfd.h>
#include <unistd.h>
#ifdef __ANDROID__
#include "framelink_ahb.h"
#include "../gst/framelink_native.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);}
#define NATIVE_FEATURE "memory:FrameLinkAHardwareBuffer"
#define NATIVE_RESOURCE FL_MEMORY_AHB
#define NATIVE_NAME "AHardwareBuffer"
#else
#include "framelink_dmabuf.h"
#include <gst/allocators/gstdmabuf.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#define NATIVE_FEATURE "memory:DMABuf"
#define NATIVE_RESOURCE FL_MEMORY_DMABUF
#define NATIVE_NAME "DMA-BUF"
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

// Per-byte positional content: a stale, shifted or channel-swapped byte fails.
static guint8 pattern(guint8 frame,unsigned plane,unsigned x,unsigned y){return guint8(frame*29+plane*101+x*7+y*13);}
// Geometry written and checked; a bench run writes and checks only its first rows.
struct Extent{unsigned width,height,rows;};

static GstFrameLinkMemoryPolicy nativeAvPolicy(bool nativeReceiver,bool renderProducer=false,uint32_t resource=NATIVE_RESOURCE){
 GstFrameLinkMemoryPolicy policy{};policy.version=1;
 for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);
  policy.requirements[i].allowedResourceTypes=resource;
  policy.requirements[i].allowedOperations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
  policy.paths[i].version=1;policy.paths[i].resourceType=resource;
 }
 policy.paths[0].supportedUsage=policy.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE;
 policy.paths[1].supportedUsage=policy.requirements[1].requiredUsage=FL_MEMORY_CPU_READ;
 policy.paths[1].operations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
 if(nativeReceiver){
  for(unsigned i=0;i<2;++i){policy.requirements[i].allowedOperations=0;policy.paths[i].operations=0;
   policy.paths[i].supportedUsage=policy.requirements[i].requiredUsage=FL_MEMORY_GPU_SAMPLE;}
 }
 if(renderProducer)policy.paths[0].supportedUsage=policy.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE|FL_MEMORY_GPU_RENDER;
 return policy;
}
// The appsink reader finishes its CPU oracle before dropping the memory; a
// real GPU reader must retain it through its actual completion.
static gboolean readerAllocation(GstElement*,GstQuery*query,gpointer data){
 auto*policy=static_cast<GstFrameLinkMemoryPolicy*>(data);
 guint8 wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];
 CHECK(flMemoryRequirementsEncode(&policy->requirements[1],wire,sizeof wire)==FL_MEMORY_OK);
 auto*bytes=g_bytes_new(wire,sizeof wire);
 gst_structure_set(gst_query_writable_structure(query),
  "framelink-reader-completion-contract",G_TYPE_STRING,"retain-memory-until-gpu-complete-v1",
  "framelink-memory-requirements",G_TYPE_BYTES,bytes,nullptr);
 g_bytes_unref(bytes);return TRUE;
}
static void terminal(GstElement*p,GstMessageType expected){
 auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
 if(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);fprintf(stderr,"native AV: %s %s\n",e->message,d?d:"");g_clear_error(&e);g_free(d);}
 CHECK(m&&GST_MESSAGE_TYPE(m)==expected);gst_message_unref(m);gst_object_unref(bus);
}
static void dumpErrors(GstElement*tx,GstElement*rx){
 for(auto*pipeline:{tx,rx}){auto*bus=gst_element_get_bus(pipeline);
  while(auto*msg=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR)){GError*error=nullptr;gchar*debug=nullptr;gst_message_parse_error(msg,&error,&debug);
   fprintf(stderr,"pipeline error: %s %s\n",error->message,debug?debug:"");g_clear_error(&error);g_free(debug);gst_message_unref(msg);}
  gst_object_unref(bus);}
}
static std::string tempSessionPath(const char*tag){
 gchar*dir=g_dir_make_tmp((std::string(tag)+"-XXXXXX").c_str(),nullptr);CHECK(dir);
 std::string path=std::string(dir)+"/session";g_free(dir);return path;
}
static FlGstSession*childSession(GstElement*bin){
 auto*child=gst_bin_get_by_name(GST_BIN(bin),"video");CHECK(child);
 GstState state=GST_STATE_NULL;const auto end=g_get_monotonic_time()+5000000;
 do{gst_element_get_state(child,&state,nullptr,0);if(state==GST_STATE_PLAYING)break;g_usleep(1000);}while(g_get_monotonic_time()<end);
 CHECK(state==GST_STATE_PLAYING);auto*context=gst_element_get_context(child,"framelink.session.v1");CHECK(context);
 auto*session=fl_gst_session_from_context(context);CHECK(session);gst_context_unref(context);gst_object_unref(child);return session;
}
// Platform layer: write source content, identify an allocation, check that a
// received memory is native, and read it back for the pixel oracle.
#ifdef __ANDROID__
static AHardwareBuffer*hardwareBuffer(GstMemory*mem){auto*ahb=gst_framelink_memory_get_hardware_buffer(mem);CHECK(ahb);return ahb;}
static void fill(GstBuffer*b,bool nv12,guint8 value,Extent e){
 CHECK(!nv12); // Android native output is exact RGB only
 auto*ahb=hardwareBuffer(gst_buffer_peek_memory(b,0));AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(ahb,&desc);void*data=nullptr;
 // CPU-mappable rings carry the cached read hint wherever the device supports it.
 auto cached=desc;cached.usage=(cached.usage&~uint64_t(AHARDWAREBUFFER_USAGE_CPU_READ_MASK))|AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
 if(AHardwareBuffer_isSupported(&cached))CHECK((desc.usage&AHARDWAREBUFFER_USAGE_CPU_READ_MASK)==AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN);
 CHECK(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data)==0);
 for(unsigned y=0;y<e.rows;++y)for(unsigned x=0;x<e.width*4;++x)((guint8*)data)[y*desc.stride*4+x]=pattern(value,0,x,y);
 CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0); // source content; the sink never maps or copies
}
static uint64_t allocationId(GstMemory*mem){
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);static auto getId=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");
 CHECK(getId);uint64_t id=0;CHECK(getId(hardwareBuffer(mem),&id)==0);return id;
}
static bool isNative(GstMemory*mem){return gst_framelink_memory_get_hardware_buffer(mem)!=nullptr;}
static void checkPixels(GstMemory*mem,GstSample*,bool nativeReceiver,bool nv12,guint8 value,Extent e){
 CHECK(!nv12);
 if(nativeReceiver){
  auto*ahb=hardwareBuffer(mem);AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(ahb,&desc);void*data=nullptr;
  CHECK(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,nullptr,&data)==0);
  for(unsigned y=0;y<e.rows;++y)for(unsigned x=0;x<e.width*4;++x)CHECK(((guint8*)data)[y*desc.stride*4+x]==pattern(value,0,x,y));
  CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0);return;
 }
 GstMapInfo map{};CHECK(gst_memory_map(mem,&map,GST_MAP_READ));
 for(unsigned y=0;y<e.rows;++y)for(unsigned x=0;x<e.width*4;++x)CHECK(map.data[y*e.width*4+x]==pattern(value,0,x,y));gst_memory_unmap(mem,&map);
}
#else
static void fill(GstBuffer*b,bool nv12,guint8 value,Extent e){
 auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_dmabuf_memory(mem));const int fd=gst_dmabuf_memory_get_fd(mem);
 auto*layout=gst_buffer_get_video_meta(b);CHECK(layout);
 dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 void*data=mmap(nullptr,mem->size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(data!=MAP_FAILED);
 for(unsigned plane=0;plane<(nv12?2u:1u);++plane)for(unsigned y=0;y<(plane?e.rows/2:e.rows);++y)
  for(unsigned x=0;x<(nv12?e.width:e.width*4);++x)((guint8*)data)[layout->offset[plane]+y*layout->stride[plane]+x]=pattern(value,plane,x,y);
 CHECK(munmap(data,mem->size)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
}
static uint64_t allocationId(GstMemory*mem){struct stat st{};CHECK(fstat(gst_dmabuf_memory_get_fd(mem),&st)==0);return uint64_t(st.st_ino);}
static bool isNative(GstMemory*mem){return gst_is_dmabuf_memory(mem);}
static void checkPixels(GstMemory*mem,GstSample*sample,bool nativeReceiver,bool nv12,guint8 value,Extent e){
 // Native memory carries its real layout; packed CPU output follows its caps.
 auto*vb=gst_sample_get_buffer(sample);
 GstVideoInfo info;CHECK(gst_video_info_from_caps(&info,gst_sample_get_caps(sample)));
 gsize offset[2]={GST_VIDEO_INFO_PLANE_OFFSET(&info,0),GST_VIDEO_INFO_PLANE_OFFSET(&info,1)};
 gint stride[2]={GST_VIDEO_INFO_PLANE_STRIDE(&info,0),GST_VIDEO_INFO_PLANE_STRIDE(&info,1)};
 if(auto*meta=gst_buffer_get_video_meta(vb)){for(unsigned i=0;i<2;++i){offset[i]=meta->offset[i];stride[i]=meta->stride[i];}}
 else CHECK(!nativeReceiver);
 GstMapInfo map{};CHECK(gst_memory_map(mem,&map,GST_MAP_READ));
 for(unsigned plane=0;plane<(nv12?2u:1u);++plane)for(unsigned y=0;y<(plane?e.rows/2:e.rows);++y)for(unsigned x=0;x<(nv12?e.width:e.width*4);++x)
  CHECK(map.data[offset[plane]+y*stride[plane]+x]==pattern(value,plane,x,y));
 gst_memory_unmap(mem,&map);
}
#endif

struct BinRun{
 bool nativeReceiver=false;const char*format="RGBA";bool renderProducer=false;
 bool gpuReadback=false;  // readback receiver through the EGL staging adapter
 unsigned width=64,height=32,count=8;
 bool bench=false;        // time push to pull; write and check only the first rows
};
// The property exists on every build; CMake says whether this one can stage.
static bool hasGpuReadback(){
#ifdef FL_TEST_EGL_READBACK
 return true;
#else
 return false;
#endif
}

// Real bins on both ends, explicit candidates, the sink's own native pool.
static void nativeAvBins(const BinRun&run){
 const bool nativeReceiver=run.nativeReceiver,renderProducer=run.renderProducer;const char*format=run.format;
 const bool nv12=!strcmp(format,"NV12");
 const Extent extent{run.width,run.height,run.bench?4u:run.height};
 const std::string sizeCaps=",width="+std::to_string(run.width)+",height="+std::to_string(run.height);
 const std::string videoCaps=std::string("video/x-raw(" NATIVE_FEATURE "),format=")+format+sizeCaps+",framerate=30/1";
 GError*e=nullptr;
 auto*tx=gst_parse_launch(("framelinkavsink name=transport native-memory=true appsrc name=vi is-live=true format=time caps="+videoCaps+
  " ! transport.video_sink appsrc name=ai is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! transport.audio_sink").c_str(),&e);
 if(e)fprintf(stderr,"producer pipeline: %s\n",e->message);CHECK(tx&&!e);
 auto*rx=gst_parse_launch((std::string("framelinkavsrc name=transport video-format=")+format+" width="+std::to_string(run.width)+" height="+std::to_string(run.height)+" framerate=30/1 transport.video_src ! appsink name=vo sync=false async=false transport.audio_src ! appsink name=ao sync=false async=false").c_str(),&e);CHECK(rx&&!e);
 auto*sink=gst_bin_get_by_name(GST_BIN(tx),"transport");auto*src=gst_bin_get_by_name(GST_BIN(rx),"transport");
 if(nativeReceiver)g_object_set(src,"native-memory",TRUE,nullptr);
 if(run.gpuReadback)g_object_set(src,"gpu-readback",TRUE,nullptr); // before the candidate freezes properties
 const auto path=tempSessionPath("framelink-native-av");
 for(auto*bin:{sink,src})g_object_set(bin,"session",path.c_str(),"video-format",format,"width",run.width,"height",run.height,
   "video-pool",2u,"video-queue-frames",1u,"connect-timeout-ms",2000u,"idle-timeout-ms",1000u,nullptr);
 auto policy=nativeAvPolicy(nativeReceiver,renderProducer);
 for(auto*bin:{sink,src}){auto candidate=policy;gboolean accepted=FALSE;
  g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(accepted);
  candidate.version=99; // the bin keeps its own copy
 }
 auto*vi=gst_bin_get_by_name(GST_BIN(tx),"vi");auto*ai=gst_bin_get_by_name(GST_BIN(tx),"ai");
 auto*vo=gst_bin_get_by_name(GST_BIN(rx),"vo");auto*ao=gst_bin_get_by_name(GST_BIN(rx),"ao");
 if(nativeReceiver){
  g_object_set(vo,"emit-signals",TRUE,"enable-last-sample",FALSE,nullptr);
  g_signal_connect(vo,"propose-allocation",G_CALLBACK(readerAllocation),&policy);
 }
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*s=childSession(sink);auto*r=childSession(src);
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 CHECK(cfg.epochNs==gint64(gst_element_get_base_time(tx)));
 flSessionConfig remote{};CHECK(fl_gst_session_get_config(r,&remote)==FL_GST_SESSION_READY&&remote.generation==cfg.generation&&remote.epochNs==cfg.epochNs);
 gint64 mappedBase=0;CHECK(fl_gst_session_map_running_time(r,cfg.generation,0,TRUE,&mappedBase)==FL_GST_SESSION_READY);

 auto*caps=gst_caps_from_string(videoCaps.c_str());auto*q=gst_query_new_allocation(caps,TRUE);
 guint8 wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&policy.requirements[0],wire,sizeof wire)==FL_MEMORY_OK);
 auto*bytes=g_bytes_new(wire,sizeof wire);
 gst_structure_set(gst_query_writable_structure(q),"framelink-memory-requirements",G_TYPE_BYTES,bytes,
  "framelink-completion-contract",G_TYPE_STRING,"external-sync-fd-before-submit-v1",nullptr);g_bytes_unref(bytes);
 auto*pad=gst_element_get_static_pad(sink,"video_sink");CHECK(gst_pad_query(pad,q));gst_object_unref(pad);
 GstBufferPool*pool=nullptr;guint size=0,min=0,max=0;gst_query_parse_nth_allocation_pool(q,0,&pool,&size,&min,&max);
 CHECK(pool&&max==2);gst_query_unref(q);gst_caps_unref(caps);CHECK(gst_buffer_pool_set_active(pool,TRUE));

 // Contiguous 1 ms PCM blocks; each frame lasts 1 ms and leads its audio by 37 ms.
 const unsigned count=run.count;const GstClockTime videoDuration=1000000;
 std::set<uint64_t> producerAllocations;std::vector<int> readyFds;GstClockTime finalPts=0;std::vector<double> receiveMs;
 for(unsigned n=0;n<count;++n){
  GstBuffer*b=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK);
  producerAllocations.insert(allocationId(gst_buffer_peek_memory(b,0)));
  const guint8 value=guint8(n+1);fill(b,nv12,value,extent);
  const int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);readyFds.push_back(ready);
  uint64_t one=1;CHECK(write(ready,&one,sizeof one)==sizeof one); // CPU writes are complete
  gboolean accepted=FALSE;g_signal_emit_by_name(sink,"set-native-ready-fd",b,ready,&accepted);CHECK(accepted);
  const GstClockTime audioPts=GstClockTime(n)*1000000;
  GST_BUFFER_PTS(b)=finalPts=37000000+audioPts;GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b)-100;GST_BUFFER_DURATION(b)=videoDuration;
  const gsize pcmBytes=192;
  auto*a=gst_buffer_new_allocate(nullptr,pcmBytes,nullptr);gst_buffer_memset(a,0,n+31,pcmBytes);
  GST_BUFFER_PTS(a)=audioPts;GST_BUFFER_DURATION(a)=1000000;
  const gint64 pushed=g_get_monotonic_time();
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(vi),b)==GST_FLOW_OK);CHECK(gst_app_src_push_buffer(GST_APP_SRC(ai),a)==GST_FLOW_OK);
  auto*vs=gst_app_sink_try_pull_sample(GST_APP_SINK(vo),2*GST_SECOND);receiveMs.push_back((g_get_monotonic_time()-pushed)/1000.0);auto*as=gst_app_sink_try_pull_sample(GST_APP_SINK(ao),2*GST_SECOND);
  if(!vs||!as){fprintf(stderr,"%s receiver=%s frame=%u video=%p audio=%p\n",format,nativeReceiver?"native":"readback",n,(void*)vs,(void*)as);dumpErrors(tx,rx);}
  CHECK(vs&&as);
  auto*vb=gst_sample_get_buffer(vs);auto*ab=gst_sample_get_buffer(as);
  CHECK(GST_BUFFER_PTS(vb)==guint64(mappedBase)+37000000+audioPts);
  CHECK(GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb)-100&&GST_BUFFER_DURATION(vb)==videoDuration);
  CHECK(GST_BUFFER_PTS(ab)==guint64(mappedBase)+audioPts&&GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000);
  CHECK(gst_buffer_n_memory(vb)==1);auto*memory=gst_buffer_peek_memory(vb,0);
  if(nativeReceiver){
   // Allocation identity: the receiver holds the producer's own allocation.
   CHECK(isNative(memory));CHECK(producerAllocations.count(allocationId(memory))==1);
  }
  checkPixels(memory,vs,nativeReceiver,nv12,value,extent);
  std::vector<guint8>pcm(pcmBytes);CHECK(gst_buffer_get_size(ab)==pcmBytes&&gst_buffer_extract(ab,0,pcm.data(),pcmBytes)==pcmBytes);
  for(auto v:pcm)CHECK(v==n+31);
  gst_sample_unref(vs);gst_sample_unref(as);
 }
 // A two-slot ring cycles; more distinct allocations would mean copies.
 CHECK(producerAllocations.size()==2);
 CHECK(gst_app_src_end_of_stream(GST_APP_SRC(vi))==GST_FLOW_OK);CHECK(gst_app_src_end_of_stream(GST_APP_SRC(ai))==GST_FLOW_OK);
 terminal(tx,GST_MESSAGE_EOS);terminal(rx,GST_MESSAGE_EOS);
 guint64 frames=999;gint64 last=999;
 CHECK(fl_gst_session_wait_video_eos(r,cfg.generation,0,&frames,&last)==FL_GST_SESSION_READY&&frames==count&&last==gint64(finalPts));
 const auto start=g_get_monotonic_time();
 gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);CHECK(g_get_monotonic_time()-start<1000000);
 CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
 for(int fd:readyFds)close(fd);
 g_object_unref(s);g_object_unref(r);
 for(auto*o:{vi,ai,vo,ao,sink,src})gst_object_unref(o);gst_object_unref(tx);gst_object_unref(rx);
 const char*receiver=nativeReceiver?"native":run.gpuReadback?"GPU-staged readback":"CPU readback";
 if(run.bench){
  // Push to pull of one frame at a time: a stage cost, not paced throughput.
  std::vector<double> sorted(receiveMs.begin()+1,receiveMs.end());std::sort(sorted.begin(),sorted.end());
  auto at=[&](double q){return sorted[std::min(sorted.size()-1,size_t(q*sorted.size()))];};
  printf("bench %s %ux%u %s receiver: frames=%u first=%.3f p50=%.3f p90=%.3f p99=%.3f max=%.3f ms\n",format,run.width,run.height,receiver,count,
   receiveMs[0],at(0.5),at(0.9),at(0.99),sorted.back());
  return;
 }
 printf("%s native " NATIVE_NAME " A/V bins, %s receiver%s: %u frames, original allocations, pixels, PCM and A/V offset pass\n",
  format,receiver,renderProducer?", sample+render producer":"",count);
}

// Direct API: one positive setup, then invalid policies, candidate mismatch,
// isolation from the plain A/V profile, capacity and cancellation.
static void nativeLocalSetup(bool nativeReceiver){
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};
 caps.audioCount=1;caps.audio[0]={FL_PCM_S16,48000,2};
 {
  auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);const auto path=tempSessionPath("framelink-native-local");
  auto candidate=nativeAvPolicy(nativeReceiver);GstFrameLinkMemoryPolicy a{},b{};flChannel*pch=nullptr,*rch=nullptr;
  gboolean peer=FALSE;GError*se=nullptr,*ce=nullptr;
  std::thread receiver([&]{peer=fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,1,2,80,2000,1000,&candidate,&b,&rch,nullptr,&ce);});
  const gboolean sender=fl_gst_session_open_local_native(s,TRUE,path.c_str(),&caps,1,2,80,2000,1000,&candidate,&a,&pch,nullptr,&se);
  receiver.join();if(se||ce)fprintf(stderr,"local native: %s / %s\n",se?se->message:"ok",ce?ce->message:"ok");
  CHECK(sender&&peer&&!se&&!ce&&pch&&rch);
  CHECK(a.sessionGeneration&&a.sessionGeneration==b.sessionGeneration&&a.paths[0].resourceType==NATIVE_RESOURCE);
  flChannelInfo info{};CHECK(flQuery(rch,&info)==FL_OK&&info.width==64&&info.height==32&&info.poolSize==2);
  flClose(pch);flClose(rch);g_object_unref(s);g_object_unref(r);
 }
 for(unsigned mode=0;mode<12;++mode){
  auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);const auto path=tempSessionPath("framelink-native-negative");
  auto candidate=nativeAvPolicy(nativeReceiver),other=candidate;GstFrameLinkMemoryPolicy a{},b{};
  flChannel*pch=nullptr,*rch=nullptr;gboolean peer=TRUE;const auto start=g_get_monotonic_time();
  if(mode<6){
   if(mode==0)candidate.sessionGeneration=1;
   if(mode==1)candidate.paths[1].operations=nativeReceiver?FL_MEMORY_COPY:0;
   if(mode==2)candidate.paths[0].resourceType=FL_MEMORY_SHARED_CPU;
   if(mode==3)candidate.paths[0].operations=FL_MEMORY_COPY;
   if(mode==4)candidate.paths[0].supportedUsage=FL_MEMORY_GPU_RENDER;
   if(mode==5)candidate=nativeAvPolicy(nativeReceiver,false,FL_MEMORY_D3D11); // not this platform's resource
   a.version=99;GError*error=nullptr;
   CHECK(!fl_gst_session_open_local_native(s,TRUE,path.c_str(),&caps,1,2,80,200,1000,&candidate,&a,&pch,nullptr,&error));
   CHECK(!pch&&!a.version&&!a.sessionGeneration);
   if(mode==5)CHECK(error&&strstr(error->message,NATIVE_NAME));g_clear_error(&error);
  }else if(mode<9){
   if(mode==6)other.requirements[0].preferredUsage=FL_MEMORY_GPU_SAMPLE; // valid but distinct candidate
   std::thread receiver([&]{
    if(mode==7)peer=fl_gst_session_open_local_av(r,FALSE,path.c_str(),&caps,1,2,80,200,1000,0,&rch,nullptr,nullptr);
    else peer=fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,mode==8?2:1,2,80,200,1000,&other,&b,&rch,nullptr,nullptr);
   });
   CHECK(!fl_gst_session_open_local_native(s,TRUE,path.c_str(),&caps,mode==8?2:1,2,80,200,1000,&candidate,&a,&pch,nullptr,nullptr));
   receiver.join();CHECK(!peer&&!pch&&!rch&&!a.version&&!b.version);
  }else if(mode==9){
   struct Probe{unsigned calls=0;} probe;GError*error=nullptr;
   auto negative=[](gpointer data,GstClockTime)->gint64{++static_cast<Probe*>(data)->calls;return -1;};
   std::thread receiver([&]{peer=fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,1,2,80,500,1000,&other,&b,&rch,nullptr,nullptr);});
   CHECK(!fl_gst_session_open_local_native_epoch(s,TRUE,path.c_str(),&caps,1,2,80,500,1000,&candidate,&a,&pch,negative,&probe,nullptr,&error));
   receiver.join();CHECK(error&&strstr(error->message,"invalid session epoch"));g_clear_error(&error);
   CHECK(probe.calls==1&&!peer&&!pch&&!rch&&!a.version&&!b.version);
  }else{
   auto*cancel=g_cancellable_new();
   std::thread task([&]{peer=fl_gst_session_open_local_native(s,mode==10,path.c_str(),&caps,1,2,80,60000,1000,&candidate,&a,&pch,cancel,nullptr);});
   g_usleep(30000);g_cancellable_cancel(cancel);task.join();g_object_unref(cancel);
   CHECK(!peer&&!pch&&!a.version);
  }
  CHECK(g_get_monotonic_time()-start<1500000);g_object_unref(s);g_object_unref(r);
 }
 printf("native local setup (%s receiver): positive, invalid policies, foreign resource, candidate mismatch, plain-profile isolation, capacity, epoch and cancellation pass\n",
  nativeReceiver?"native":"CPU readback");
}

static void nativeBinNegatives(bool nativeReceiver){
 for(bool producer:{false,true}){
  for(unsigned mode=0;mode<14;++mode){
   auto*bin=gst_element_factory_make(producer?"framelinkavsink":"framelinkavsrc",nullptr);CHECK(bin);
   g_object_set(bin,"session","unused-native-negative","native-memory",producer||nativeReceiver,nullptr);
   auto candidate=nativeAvPolicy(nativeReceiver);
   if(mode==0)candidate.version=2;
   if(mode==1)candidate.sessionGeneration=1;
   if(mode==2)candidate.paths[1].operations=nativeReceiver?FL_MEMORY_COPY:0;
   if(mode==3)candidate.paths[0].operations=FL_MEMORY_COPY;
   if(mode==4)g_object_set(bin,"session",nullptr,nullptr);
   if(mode==5)g_object_set(bin,"shared-cpu",TRUE,nullptr);
   if(mode==6)g_object_set(bin,"tracks","audio",nullptr);
   if(mode==7)g_object_set(bin,"native-memory",!(producer||nativeReceiver),nullptr);
   if(mode==10)candidate.paths[0].supportedUsage=candidate.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE|FL_MEMORY_GPU_RENDER|FL_MEMORY_CPU_WRITE;
   if(mode==11)candidate.paths[0].supportedUsage=candidate.requirements[0].requiredUsage=FL_MEMORY_GPU_RENDER;
   if(mode==12)candidate=nativeAvPolicy(nativeReceiver,true);
   if(mode==13)candidate=nativeAvPolicy(nativeReceiver,false,FL_MEMORY_D3D11);
   gboolean accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-candidate",mode==8?nullptr:&candidate,&accepted);
   CHECK(accepted==(mode==9||mode==12));
   if(mode==9||mode==12){
    accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);
    accepted=TRUE;g_signal_emit_by_name(bin,"apply-memory-policy",&candidate,&accepted);CHECK(!accepted);
    accepted=TRUE;g_signal_emit_by_name(bin,"adopt-channel",nullptr,&accepted);CHECK(!accepted);
    g_object_set(bin,"width",96u,nullptr);guint width=0;g_object_get(bin,"width",&width,nullptr);CHECK(width==1280);
   }
   gst_object_unref(bin);
  }
  // The FAR5 exact-pool receiver imports D3D11 handles and stays Windows-only.
  if(!producer){
   auto*bin=gst_element_factory_make("framelinkavsrc",nullptr);CHECK(bin);
   g_object_set(bin,"session","unused-native-negative","native-memory",nativeReceiver,"video-pool",2u,nullptr);
   auto candidate=nativeAvPolicy(nativeReceiver);gboolean accepted=TRUE;
   g_signal_emit_by_name(bin,"set-local-native-pool-candidate",&candidate,&accepted);CHECK(!accepted);gst_object_unref(bin);
  }
  // Setup cancellation must interrupt an absent peer's 60-second deadline.
  auto*pipeline=gst_pipeline_new(nullptr);
  auto*bin=gst_element_factory_make(producer?"framelinkavsink":"framelinkavsrc",nullptr);CHECK(bin);
  gst_bin_add(GST_BIN(pipeline),bin);const auto path=tempSessionPath("framelink-native-cancel");
  g_object_set(bin,"session",path.c_str(),"native-memory",producer||nativeReceiver,"connect-timeout-ms",60000u,nullptr);
  auto candidate=nativeAvPolicy(nativeReceiver);gboolean accepted=FALSE;
  g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(accepted);
  CHECK(gst_element_set_state(pipeline,GST_STATE_READY)!=GST_STATE_CHANGE_FAILURE);
  accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);
  g_usleep(30000);const auto start=g_get_monotonic_time();
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  CHECK(g_get_monotonic_time()-start<1000000);gst_object_unref(pipeline);
 }
 printf("native bins (%s receiver): 28 configuration controls, pool-receiver rejection and both-role bounded cancellation pass\n",
  nativeReceiver?"native":"CPU readback");
}

// gpu-readback selects GPU staging only for the negotiated readback receiver.
static void gpuReadbackNegatives(){
 auto make=[](bool native){auto*bin=gst_element_factory_make("framelinkavsrc",nullptr);CHECK(bin);
  g_object_set(bin,"session","unused-gpu-readback","native-memory",native,"gpu-readback",TRUE,nullptr);return bin;};
 {auto*bin=make(true);auto candidate=nativeAvPolicy(true);gboolean accepted=TRUE; // a native receiver has no readback
  g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);gst_object_unref(bin);}
 {auto*bin=make(false);g_object_set(bin,"shared-cpu",TRUE,nullptr);auto candidate=nativeAvPolicy(false);gboolean accepted=TRUE;
  g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);gst_object_unref(bin);}
 {auto*bin=make(false);auto candidate=nativeAvPolicy(false);gboolean accepted=FALSE; // frozen with the candidate
  g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(accepted);
  g_object_set(bin,"gpu-readback",FALSE,nullptr);gboolean value=FALSE;g_object_get(bin,"gpu-readback",&value,nullptr);CHECK(value);gst_object_unref(bin);}
 for(bool shared:{false,true}){ // no candidate: refused before any session work
  auto*pipeline=gst_pipeline_new(nullptr);auto*bin=make(false);gst_bin_add(GST_BIN(pipeline),bin);
  const auto path=tempSessionPath("framelink-gpu-readback");g_object_set(bin,"session",path.c_str(),"shared-cpu",shared,nullptr);
  CHECK(gst_element_set_state(pipeline,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);
  gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(pipeline);
 }
 puts("gpu-readback bins: native receiver, shared CPU, frozen property and missing candidate controls pass");
}

int main(int argc,char**argv){
 gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 if(!dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId")){puts("SKIP: AHardwareBuffer_getId unavailable");return 77;}
 const char*second="RGB10A2_LE"; // Android native output is exact RGB: no NV12
 const int first=1;
#else
 CHECK(argc>=2);GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
 const char*second="NV12";
 const int first=2; // after the plugin path
#endif
 // [plugin] --bench WIDTH HEIGHT FORMAT FRAMES cpu|gpu|native: receive-stage timing only.
 if(argc==first+6&&!strcmp(argv[first],"--bench")){
  const char*mode=argv[first+5];
  BinRun run;run.width=unsigned(atoi(argv[first+1]));run.height=unsigned(atoi(argv[first+2]));run.format=argv[first+3];run.count=unsigned(atoi(argv[first+4]));
  run.gpuReadback=!strcmp(mode,"gpu");run.nativeReceiver=!strcmp(mode,"native");run.bench=true;
  CHECK(run.count>=2&&(run.gpuReadback||run.nativeReceiver||!strcmp(mode,"cpu")));
  CHECK(!run.gpuReadback||hasGpuReadback());nativeAvBins(run);return 0;
 }
 CHECK(argc==first);
 for(bool nativeReceiver:{false,true}){nativeBinNegatives(nativeReceiver);nativeLocalSetup(nativeReceiver);}
 auto bins=[](bool nativeReceiver,const char*format,bool renderProducer=false,bool gpuReadback=false){
  BinRun run;run.nativeReceiver=nativeReceiver;run.format=format;run.renderProducer=renderProducer;run.gpuReadback=gpuReadback;nativeAvBins(run);};
 bins(true,"RGBA");
 bins(true,second);
 bins(true,"RGBA",true);
 bins(false,"RGBA");
 if(hasGpuReadback()){ // Linux EGL builds: explicit GPU staging for the readback receiver
  gpuReadbackNegatives();
  bins(false,"BGRA");
  bins(false,"RGBA",false,true);
  bins(false,"BGRA",false,true);
 }
 puts("PASS");
}
