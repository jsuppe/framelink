// Broker-composed process relays: three independent processes (source, relay,
// final reader) under a supervisor that owns the allocations, the test
// credentials and every process lifetime. The supervisor allocates two images
// once and hands the same native handles to both rings. Not a production
// broker, discovery or performance test.
//
// Platform parts are in fl_gst_native_relay_platform.h: Child (with inherited
// allocation handles), Gate, Mailbox, SharedImages, and the reservation owner.
#ifdef FL_RELAY_AMC_DECODER
#include <media/NdkMediaCodec.h>
#include <media/NdkImageReader.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#endif
namespace relayprocess {
static constexpr unsigned Frames=16;
static uint64_t frameTime(unsigned n,bool decoder){return decoder?gst_util_uint64_scale(n,GST_SECOND,30):n*1000000ULL;}
static uint64_t frameDuration(unsigned /*n*/,bool decoder){return frameTime(1,decoder);}
// A decoder may report either the nominal duration or the exact gap to the
// next contiguous timestamp (GStreamer rounds 1/30 s to ...33 or ...34 ns).
static bool decodedDuration(uint64_t d,unsigned n){return d==frameDuration(n,true)||d==frameTime(n+1,true)-frameTime(n,true);}
static uint64_t pixelHash(const unsigned char*data,size_t stride){
 uint64_t hash=14695981039346656037ULL;
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x){hash^=data[y*stride+x];hash*=1099511628211ULL;}return hash;
}
static bool same(flImage*a,flImage*b){return relaytest::sameAllocation(a,b);}
// Does the decoder fixture RENDER into the reservation? Windows: the D3D11
// converter does (GPU_RENDER ring and policy). Linux: CUDA copies into the
// reservation's EGLImage; NVIDIA refuses render usage for linear DMA-BUFs, so
// the ring and policy honestly declare sampling only.
// Android: GLES renders the decoded frame into the reservation's
// AHardwareBuffer (GPU_COLOR_OUTPUT), like Windows.
#if defined(_WIN32) || defined(FL_RELAY_AMC_DECODER)
static constexpr bool DecoderRenders=true;
#else
static constexpr bool DecoderRenders=false;
#endif
static uint32_t renderFlag(bool decoder){return decoder&&DecoderRenders?FL_GPU_RENDER:0;}

// Every control signal of one graph, created by the supervisor and inherited
// by each child (one command-line argument).
enum Sig{Held,Checked,Done,RelayDone,FaultChecked,Attached,Drained,ReaderReady,Never,SigCount};
struct Signals {
 std::unique_ptr<relaytest::Gate> gate[SigCount];
 Signals(){for(auto&g:gate)g.reset(new relaytest::Gate());}
 explicit Signals(const char*arg){
  std::string all(arg);size_t at=0;
  for(auto&g:gate){auto end=all.find(';',at);CHECK(end!=std::string::npos||&g==&gate[SigCount-1]);
   g.reset(new relaytest::Gate(all.substr(at,end==std::string::npos?std::string::npos:end-at).c_str()));at=end+1;}
 }
 std::string arg()const{std::string out;for(auto&g:gate){if(!out.empty())out+=";";out+=g->arg();}return out;}
 std::vector<const relaytest::Gate*> all()const{std::vector<const relaytest::Gate*> v;for(auto&g:gate)v.push_back(g.get());return v;}
 relaytest::Gate& operator[](Sig s){return *gate[s];}
};

#ifdef _WIN32
// Explicit upstream decode/color/scale into a reservation, never a relay copy.
struct Decoder {
 GstElement*pipeline=nullptr,*sink=nullptr;GstD3D11Converter*converter=nullptr;
 GstClockTime origin=GST_CLOCK_TIME_NONE;std::vector<uint64_t> hashes;
 uint64_t fill(relaytest::ReservationOwner&,GstBuffer*buffer,unsigned n){
  auto*mem=gst_buffer_peek_memory(buffer,0);auto*device=GST_D3D11_MEMORY_CAST(mem)->device;
  if(!pipeline){
   GError*error=nullptr;
   pipeline=gst_parse_launch("videotestsrc num-buffers=16 pattern=snow ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 ! h264parse ! d3d11h264dec ! video/x-raw(memory:D3D11Memory),format=NV12 ! appsink name=decoded sync=false enable-last-sample=false max-buffers=1",&error);
   CHECK(pipeline&&!error);auto*context=gst_d3d11_context_new(device);gst_element_set_context(pipeline,context);gst_context_unref(context);
   sink=gst_bin_get_by_name(GST_BIN(pipeline),"decoded");CHECK(sink);
   CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  }
  auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);CHECK(sample);
  auto*input=gst_sample_get_buffer(sample);CHECK(gst_buffer_n_memory(input)==1);
  auto*memory=gst_buffer_peek_memory(input,0);CHECK(gst_is_d3d11_memory(memory)&&GST_D3D11_MEMORY_CAST(memory)->device==device);
  CHECK(!gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),FrameLinkD3DProducer::key()));
  CHECK(GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(input))&&!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DTS(input)));
  if(!n)origin=GST_BUFFER_PTS(input);
  fprintf(stderr,"DECODED,%u,pts=%llu,duration=%llu,origin=%llu\n",n,(unsigned long long)GST_BUFFER_PTS(input),(unsigned long long)GST_BUFFER_DURATION(input),(unsigned long long)origin);
  CHECK(GST_BUFFER_PTS(input)-origin==frameTime(n,true)&&decodedDuration(GST_BUFFER_DURATION(input),n));
  if(!converter){GstVideoInfo in{},out{};CHECK(gst_video_info_from_caps(&in,gst_sample_get_caps(sample)));
   CHECK(GST_VIDEO_INFO_FORMAT(&in)==GST_VIDEO_FORMAT_NV12&&GST_VIDEO_INFO_WIDTH(&in)==320&&GST_VIDEO_INFO_HEIGHT(&in)==240);
   gst_video_info_set_format(&out,GST_VIDEO_FORMAT_RGBA,64,32);out.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;
   converter=gst_d3d11_converter_new(device,&in,&out,nullptr);CHECK(converter);}
  CHECK(gst_d3d11_converter_convert_buffer(converter,input,buffer));
  auto*d=gst_d3d11_device_get_device_handle(device);auto*c=gst_d3d11_device_get_device_context_handle(device);
  D3D11_QUERY_DESC description{D3D11_QUERY_EVENT,0};ID3D11Query*done=nullptr;CHECK(SUCCEEDED(d->CreateQuery(&description,&done)));
  gst_d3d11_device_lock(device);c->End(done);c->Flush();HRESULT result;const auto deadline=g_get_monotonic_time()+2000000;
  do{result=c->GetData(done,nullptr,0,0);if(result==S_FALSE)g_usleep(100);}while(result==S_FALSE&&g_get_monotonic_time()<deadline);
  gst_d3d11_device_unlock(device);done->Release();CHECK(result==S_OK);gst_sample_unref(sample);
  auto*meta=gst_buffer_get_video_meta(buffer);GstMapInfo map{};CHECK(meta&&gst_memory_map(mem,&map,GST_MAP_READ));
  auto*data=map.data+meta->offset[0];for(unsigned y=0;y<32;++y)for(unsigned x=3;x<256;x+=4)CHECK(data[y*meta->stride[0]+x]==255);
  const auto hash=pixelHash(data,meta->stride[0]);gst_memory_unmap(mem,&map);
  for(auto previous:hashes)CHECK(previous!=hash);hashes.push_back(hash);
  fprintf(stderr,"DECODED_PIXELS,%u,%llu\n",n,(unsigned long long)hash);return hash;
 }
 ~Decoder(){if(pipeline){CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(sink)));avTerminal(pipeline,GST_MESSAGE_EOS);
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(sink);gst_object_unref(pipeline);gst_object_unref(converter);}}
};
#elif defined(FL_RELAY_GL_DECODER)
// Linux: NVDEC decodes into GL memory, GStreamer GL converts and scales on the
// GPU, and CUDA copies the result into the reservation's own DMA-BUF through an
// EGLImage (NVIDIA imports a linear DMA-BUF for GL sampling only, never as a
// render target). No CPU copy. libcuda is opened at run time and only the
// driver-API subset below is declared, so the build needs no CUDA toolkit.
namespace cuda {
using Result=int;using Device=int;using Context=struct CUctx_st*;using Array=struct CUarray_st*;
using Resource=struct CUgraphicsResource_st*;using Stream=struct CUstream_st*;
enum {Success=0,MemoryTypeDevice=2,MemoryTypeArray=3,RegisterReadOnly=1,RegisterWriteDiscard=2,FrameTypeArray=0,FrameTypePitch=1,GlDevicesAll=1};
struct Memcpy2D { // CUDA_MEMCPY2D (v2)
 size_t srcXInBytes,srcY;int srcMemoryType;const void*srcHost;unsigned long long srcDevice;Array srcArray;size_t srcPitch;
 size_t dstXInBytes,dstY;int dstMemoryType;void*dstHost;unsigned long long dstDevice;Array dstArray;size_t dstPitch;
 size_t WidthInBytes,Height;
};
struct EglFrame { // CUeglFrame
 union{Array pArray[3];void*pPitch[3];}frame;
 unsigned width,height,depth,pitch,planeCount,numChannels;int frameType,eglColorFormat,cuFormat;
};
struct Api {
 void*library=nullptr;
 Result(*Init)(unsigned)=nullptr;Result(*GLGetDevices)(unsigned*,Device*,unsigned,int)=nullptr;
 Result(*PrimaryCtxRetain)(Context*,Device)=nullptr;Result(*PrimaryCtxRelease)(Device)=nullptr;
 Result(*CtxPush)(Context)=nullptr;Result(*CtxPop)(Context*)=nullptr;Result(*CtxSynchronize)()=nullptr;
 Result(*GLRegisterImage)(Resource*,unsigned,unsigned,unsigned)=nullptr;Result(*EGLRegisterImage)(Resource*,void*,unsigned)=nullptr;
 Result(*Map)(unsigned,Resource*,Stream)=nullptr;Result(*Unmap)(unsigned,Resource*,Stream)=nullptr;
 Result(*MappedArray)(Array*,Resource,unsigned,unsigned)=nullptr;Result(*MappedEglFrame)(EglFrame*,Resource,unsigned,unsigned)=nullptr;
 Result(*Unregister)(Resource)=nullptr;Result(*Copy2D)(const Memcpy2D*)=nullptr;Result(*ErrorName)(Result,const char**)=nullptr;
 bool load(){
  library=dlopen("libcuda.so.1",RTLD_NOW|RTLD_LOCAL);if(!library)return false;
  auto sym=[&](auto&fn,const char*name){fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(library,name));return fn!=nullptr;};
  return sym(Init,"cuInit")&&sym(GLGetDevices,"cuGLGetDevices_v2")&&sym(PrimaryCtxRetain,"cuDevicePrimaryCtxRetain")&&
   sym(PrimaryCtxRelease,"cuDevicePrimaryCtxRelease_v2")&&sym(CtxPush,"cuCtxPushCurrent_v2")&&sym(CtxPop,"cuCtxPopCurrent_v2")&&
   sym(CtxSynchronize,"cuCtxSynchronize")&&sym(GLRegisterImage,"cuGraphicsGLRegisterImage")&&sym(EGLRegisterImage,"cuGraphicsEGLRegisterImage")&&
   sym(Map,"cuGraphicsMapResources")&&sym(Unmap,"cuGraphicsUnmapResources")&&sym(MappedArray,"cuGraphicsSubResourceGetMappedArray")&&
   sym(MappedEglFrame,"cuGraphicsResourceGetMappedEglFrame")&&sym(Unregister,"cuGraphicsUnregisterResource")&&
   sym(Copy2D,"cuMemcpy2D_v2")&&sym(ErrorName,"cuGetErrorName")&&Init(0)==Success;
 }
 void check(Result r,const char*what){if(r==Success)return;const char*name="?";ErrorName(r,&name);fprintf(stderr,"%s -> %s\n",what,name);CHECK(r==Success);}
};
}
#define FL_CU(call) cu.check(cu.call,#call)
struct Decoder {
 GstElement*pipeline=nullptr,*sink=nullptr;GstClockTime origin=GST_CLOCK_TIME_NONE;std::vector<uint64_t> hashes;
 cuda::Api cu;cuda::Device device=-1;cuda::Context context=nullptr;
 struct Job{Decoder*self;GstGLMemory*source;int fd;gsize offset;gint stride;};
 // Runs in the decoder's GL context: register both images with CUDA, copy, wait.
 static void copy(GstGLContext*gl,gpointer data){
  auto*job=static_cast<Job*>(data);auto&cu=job->self->cu;
  if(!job->self->context){
   unsigned count=0;cuda::Device devices[1]{};FL_CU(GLGetDevices(&count,devices,1,cuda::GlDevicesAll));CHECK(count==1);
   job->self->device=devices[0];FL_CU(PrimaryCtxRetain(&job->self->context,devices[0]));
  }
  FL_CU(CtxPush(job->self->context));
  GstVideoInfo info;gst_video_info_set_format(&info,GST_VIDEO_FORMAT_RGBA,64,32);info.stride[0]=job->stride;info.offset[0]=job->offset;
  GstEGLImage*image=gst_egl_image_from_dmabuf(gl,job->fd,&info,0,job->offset);CHECK(image);
  cuda::Resource src=nullptr,dst=nullptr;
  FL_CU(GLRegisterImage(&src,gst_gl_memory_get_texture_id(job->source),0x0DE1/*GL_TEXTURE_2D*/,cuda::RegisterReadOnly));
  FL_CU(EGLRegisterImage(&dst,gst_egl_image_get_image(image),cuda::RegisterWriteDiscard));
  FL_CU(Map(1,&src,nullptr));cuda::Array in=nullptr;FL_CU(MappedArray(&in,src,0,0));
  cuda::EglFrame frame{};FL_CU(MappedEglFrame(&frame,dst,0,0));CHECK(frame.width==64&&frame.height==32&&frame.planeCount==1);
  cuda::Memcpy2D c{};c.srcMemoryType=cuda::MemoryTypeArray;c.srcArray=in;c.WidthInBytes=256;c.Height=32;
  if(frame.frameType==cuda::FrameTypeArray){c.dstMemoryType=cuda::MemoryTypeArray;c.dstArray=frame.frame.pArray[0];}
  else{c.dstMemoryType=cuda::MemoryTypeDevice;c.dstDevice=reinterpret_cast<uintptr_t>(frame.frame.pPitch[0]);c.dstPitch=frame.pitch;}
  FL_CU(Copy2D(&c));FL_CU(CtxSynchronize()); // GPU writes complete before the ready fd
  FL_CU(Unmap(1,&src,nullptr));FL_CU(Unregister(src));FL_CU(Unregister(dst));gst_egl_image_unref(image);
  cuda::Context popped=nullptr;FL_CU(CtxPop(&popped));
 }
 uint64_t fill(relaytest::ReservationOwner&owner,GstBuffer*buffer,unsigned n){
  if(!pipeline){
   CHECK(cu.load());GError*error=nullptr;
   pipeline=gst_parse_launch("videotestsrc num-buffers=16 pattern=snow ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 ! h264parse ! nvh264dec ! video/x-raw(memory:GLMemory),format=NV12 ! glcolorconvert ! glcolorscale ! video/x-raw(memory:GLMemory),format=RGBA,width=64,height=32 ! appsink name=decoded sync=false enable-last-sample=false max-buffers=1",&error);
   CHECK(pipeline&&!error);sink=gst_bin_get_by_name(GST_BIN(pipeline),"decoded");CHECK(sink);
   CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  }
  auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);CHECK(sample);
  auto*input=gst_sample_get_buffer(sample);CHECK(gst_buffer_n_memory(input)==1);
  auto*memory=gst_buffer_peek_memory(input,0);CHECK(gst_is_gl_memory(memory));
  CHECK(!gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),relaytest::ReservationOwner::key()));
  CHECK(GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(input)));
  if(!n)origin=GST_BUFFER_PTS(input);
  fprintf(stderr,"DECODED,%u,pts=%llu,duration=%llu,origin=%llu\n",n,(unsigned long long)GST_BUFFER_PTS(input),(unsigned long long)GST_BUFFER_DURATION(input),(unsigned long long)origin);
  CHECK(GST_BUFFER_PTS(input)-origin==frameTime(n,true)&&decodedDuration(GST_BUFFER_DURATION(input),n));
  auto*target=gst_buffer_peek_memory(buffer,0);auto*layout=gst_buffer_get_video_meta(buffer);CHECK(gst_is_dmabuf_memory(target)&&layout);
  relaytest::reservationOf(buffer);
  Job job{this,reinterpret_cast<GstGLMemory*>(memory),gst_dmabuf_memory_get_fd(target),layout->offset[0],layout->stride[0]};
  gst_gl_context_thread_add(reinterpret_cast<GstGLBaseMemory*>(memory)->context,copy,&job);
  gst_sample_unref(sample);
  const int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);uint64_t one=1;CHECK(write(ready,&one,sizeof one)==sizeof one);
  CHECK(owner.setReady(buffer,ready));close(ready);
  // Source-side verification readback of the decoded reservation (not relay work).
  const int fd=gst_dmabuf_memory_get_fd(target);dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
  void*mapped=mmap(nullptr,target->size,PROT_READ,MAP_SHARED,fd,0);CHECK(mapped!=MAP_FAILED);
  auto*data=static_cast<unsigned char*>(mapped)+layout->offset[0];
  for(unsigned y=0;y<32;++y)for(unsigned x=3;x<256;x+=4)CHECK(data[y*layout->stride[0]+x]==255);
  const auto hash=pixelHash(data,layout->stride[0]);CHECK(munmap(mapped,target->size)==0);
  sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
  for(auto previous:hashes)CHECK(previous!=hash);hashes.push_back(hash);
  fprintf(stderr,"DECODED_PIXELS,%u,%llu\n",n,(unsigned long long)hash);return hash;
 }
 ~Decoder(){
  if(pipeline){CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(sink)));avTerminal(pipeline,GST_MESSAGE_EOS);
   CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(sink);gst_object_unref(pipeline);}
  if(context)cu.PrimaryCtxRelease(device);
 }
};
#undef FL_CU
#elif defined(FL_RELAY_AMC_DECODER)
// Android: the platform's hardware H.264 decoder (NDK MediaCodec) decodes into
// an AImageReader; GLES samples the decoded AHardwareBuffer and renders it,
// converted and scaled, into the reservation's own AHardwareBuffer. The GPU
// fence of that render is the reservation's ready fd. No CPU copy. GStreamer
// only produces the H.264 stream, as on the other platforms.
// MediaCodec delivers its callbacks on binder threads, which a native
// executable (unlike an app process) does not have until it starts them.
static void startBinderThreadPool(){
 static bool started=false;if(started)return;
 void*binder=dlopen("libbinder_ndk.so",RTLD_NOW|RTLD_LOCAL);CHECK(binder);
 auto setMax=reinterpret_cast<bool(*)(uint32_t)>(dlsym(binder,"ABinderProcess_setThreadPoolMaxThreadCount"));
 auto start=reinterpret_cast<void(*)()>(dlsym(binder,"ABinderProcess_startThreadPool"));
 CHECK(setMax&&start);setMax(4);start();started=true;
}
// The default H.264 decoder, which must be a hardware one (a software
// decoder would make this a CPU decode). Returns nullptr with a reason.
static AMediaCodec* hardwareAvcDecoder(std::string*name,std::string*why){
 startBinderThreadPool();
 AMediaCodec*codec=AMediaCodec_createDecoderByType("video/avc");if(!codec){*why="no H.264 MediaCodec decoder";return nullptr;}
 char*n=nullptr;if(AMediaCodec_getName(codec,&n)!=AMEDIA_OK||!n){AMediaCodec_delete(codec);*why="MediaCodec decoder has no name";return nullptr;}
 *name=n;AMediaCodec_releaseName(codec,n);
 if(name->rfind("c2.android.",0)==0||name->rfind("OMX.google.",0)==0){AMediaCodec_delete(codec);*why="the default H.264 decoder "+*name+" is a software codec";return nullptr;}
 return codec;
}
static GLuint compile(GLenum type,const char*source){
 GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[512]{};glGetShaderInfoLog(s,sizeof log,nullptr,log);fprintf(stderr,"shader: %s\n",log);}CHECK(ok);return s;
}
struct Decoder {
 GstElement*pipeline=nullptr,*sink=nullptr;AMediaCodec*codec=nullptr;AImageReader*reader=nullptr;
 EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;GLuint program=0,vao=0,source=0,target=0,fbo=0;
 // Extension entry points, resolved at run time (no prototype macros needed).
 PFNEGLDUPNATIVEFENCEFDANDROIDPROC dupFence=nullptr;PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC clientBuffer=nullptr;
 PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
 PFNEGLCREATESYNCKHRPROC createSync=nullptr;PFNEGLDESTROYSYNCKHRPROC destroySync=nullptr;
 PFNGLEGLIMAGETARGETTEXTURE2DOESPROC targetTexture=nullptr;
 bool inputDone=false,outputDone=false;
 int64_t origin=-1;std::vector<uint64_t> hashes;
 void start(){
  GError*error=nullptr;
  pipeline=gst_parse_launch("videotestsrc num-buffers=16 pattern=snow ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=encoded sync=false enable-last-sample=false",&error);
  CHECK(pipeline&&!error);sink=gst_bin_get_by_name(GST_BIN(pipeline),"encoded");CHECK(sink);
  CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  display=eglGetDisplay(EGL_DEFAULT_DISPLAY);CHECK(display!=EGL_NO_DISPLAY&&eglInitialize(display,nullptr,nullptr));
  const EGLint config[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};EGLConfig chosen{};EGLint count=0;
  CHECK(eglChooseConfig(display,config,&chosen,1,&count)&&count==1);const EGLint attributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  context=eglCreateContext(display,chosen,EGL_NO_CONTEXT,attributes);CHECK(context!=EGL_NO_CONTEXT);
  CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
  auto proc=[](const char*name){auto*f=eglGetProcAddress(name);if(!f)fprintf(stderr,"missing %s\n",name);CHECK(f);return f;};
  dupFence=reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(proc("eglDupNativeFenceFDANDROID"));
  clientBuffer=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(proc("eglGetNativeClientBufferANDROID"));
  createImage=reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(proc("eglCreateImageKHR"));destroyImage=reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(proc("eglDestroyImageKHR"));
  createSync=reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(proc("eglCreateSyncKHR"));destroySync=reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(proc("eglDestroySyncKHR"));
  targetTexture=reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(proc("glEGLImageTargetTexture2DOES"));
  const char*vs="#version 300 es\nout vec2 uv;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
  const char*fs="#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\nprecision mediump float;uniform samplerExternalOES decoded;in vec2 uv;out vec4 color;"
                "void main(){color=vec4(texture(decoded,vec2(uv.x,1.0-uv.y)).rgb,1.0);}";
  program=glCreateProgram();glAttachShader(program,compile(GL_VERTEX_SHADER,vs));glAttachShader(program,compile(GL_FRAGMENT_SHADER,fs));glLinkProgram(program);
  GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);CHECK(linked);
  glGenVertexArrays(1,&vao);glGenTextures(1,&source);glGenTextures(1,&target);glGenFramebuffers(1,&fbo);
  CHECK(AImageReader_newWithUsage(320,240,AIMAGE_FORMAT_PRIVATE,AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,4,&reader)==AMEDIA_OK);
  ANativeWindow*window=nullptr;CHECK(AImageReader_getWindow(reader,&window)==AMEDIA_OK);
  std::string name,why;codec=hardwareAvcDecoder(&name,&why);if(!codec)fprintf(stderr,"decoder: %s\n",why.c_str());CHECK(codec);
  fprintf(stderr,"DECODER,%s\n",name.c_str());
  auto*format=AMediaFormat_new();AMediaFormat_setString(format,AMEDIAFORMAT_KEY_MIME,"video/avc");
  AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_WIDTH,320);AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_HEIGHT,240);
  CHECK(AMediaCodec_configure(codec,format,window,nullptr,0)==AMEDIA_OK);AMediaFormat_delete(format);CHECK(AMediaCodec_start(codec)==AMEDIA_OK);
 }
 // Feeds access units until the decoder renders one frame; returns its image.
 AImage* next(){
  const auto deadline=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
  while(g_get_monotonic_time()<deadline){
   if(!inputDone){const ssize_t i=AMediaCodec_dequeueInputBuffer(codec,1000);
    if(i>=0){size_t capacity=0;auto*data=AMediaCodec_getInputBuffer(codec,i,&capacity);CHECK(data);
     auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);
     if(!sample){CHECK(gst_app_sink_is_eos(GST_APP_SINK(sink)));CHECK(AMediaCodec_queueInputBuffer(codec,i,0,0,0,AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)==AMEDIA_OK);inputDone=true;}
     else{auto*au=gst_sample_get_buffer(sample);const gsize size=gst_buffer_get_size(au);CHECK(size<=capacity&&GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(au)));
      CHECK(gst_buffer_extract(au,0,data,size)==size);
      CHECK(AMediaCodec_queueInputBuffer(codec,i,0,size,GST_BUFFER_PTS(au)/1000,0)==AMEDIA_OK);gst_sample_unref(sample);}}}
   AMediaCodecBufferInfo info{};const ssize_t o=AMediaCodec_dequeueOutputBuffer(codec,&info,1000);
   if(o<0)continue;
   const bool end=info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
   CHECK(AMediaCodec_releaseOutputBuffer(codec,o,info.size>0)==AMEDIA_OK);
   if(end)outputDone=true;
   if(info.size<=0){CHECK(!end);continue;}
   AImage*image=nullptr;media_status_t s;
   while((s=AImageReader_acquireNextImage(reader,&image))==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE&&g_get_monotonic_time()<deadline)g_usleep(200);
   CHECK(s==AMEDIA_OK&&image);return image;
  }
  CHECK(!"decoder produced no frame within 5 s");return nullptr;
 }
 uint64_t fill(relaytest::ReservationOwner&owner,GstBuffer*buffer,unsigned n){
  if(!pipeline)start();
  CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
  AImage*image=next();int64_t timestamp=0;CHECK(AImage_getTimestamp(image,&timestamp)==AMEDIA_OK);
  if(origin<0)origin=timestamp;
  fprintf(stderr,"DECODED,%u,pts=%lld,origin=%lld\n",n,(long long)timestamp,(long long)origin);
  // MediaCodec carries microseconds: 1/30 s steps rounded down to whole us.
  CHECK(uint64_t(timestamp-origin)==frameTime(n,true)/1000*1000);
  AHardwareBuffer*decoded=nullptr;CHECK(AImage_getHardwareBuffer(image,&decoded)==AMEDIA_OK&&decoded);
  auto*memory=gst_buffer_peek_memory(buffer,0);relaytest::reservationOf(buffer);
  AHardwareBuffer*reserved=gst_framelink_memory_get_hardware_buffer(memory);CHECK(reserved&&reserved!=decoded);
  AHardwareBuffer_Desc rd{};AHardwareBuffer_describe(reserved,&rd);
  CHECK(rd.width==64&&rd.height==32&&rd.format==AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM&&(rd.usage&AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT));
  const EGLint keep[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
  EGLImageKHR in=createImage(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,clientBuffer(decoded),keep);CHECK(in!=EGL_NO_IMAGE_KHR);
  EGLImageKHR out=createImage(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,clientBuffer(reserved),keep);CHECK(out!=EGL_NO_IMAGE_KHR);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES,source);targetTexture(GL_TEXTURE_EXTERNAL_OES,in);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D,target);targetTexture(GL_TEXTURE_2D,out);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glViewport(0,0,64,32);glUseProgram(program);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
  EGLSyncKHR rendered=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(rendered!=EGL_NO_SYNC_KHR);glFlush();
  const int ready=dupFence(display,rendered);CHECK(ready>=0);destroySync(display,rendered);CHECK(glGetError()==GL_NO_ERROR);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
  CHECK(owner.setReady(buffer,ready)); // the GPU's own completion, not a pre-signalled fd
  // The decoded image goes back to the decoder once the render has read it.
  const int released=fcntl(ready,F_DUPFD_CLOEXEC,0);CHECK(released>=0);AImage_deleteAsync(image,released);
  destroyImage(display,in);destroyImage(display,out);
  // Source-side verification readback of the rendered reservation (not relay
  // work); the lock waits on the render fence.
  void*data=nullptr;CHECK(AHardwareBuffer_lock(reserved,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,ready,nullptr,&data)==0&&data);
  auto*pixels=static_cast<unsigned char*>(data);const size_t stride=size_t(rd.stride)*4;
  for(unsigned y=0;y<32;++y)for(unsigned x=3;x<256;x+=4)CHECK(pixels[y*stride+x]==255);
  const auto hash=pixelHash(pixels,stride);CHECK(AHardwareBuffer_unlock(reserved,nullptr)==0);
  for(auto previous:hashes)CHECK(previous!=hash);hashes.push_back(hash);
  fprintf(stderr,"DECODED_PIXELS,%u,%llu\n",n,(unsigned long long)hash);return hash;
 }
 ~Decoder(){
  if(!pipeline)return;
  // Exactly 16 frames: the decoder then reports end of stream with no frame.
  const auto deadline=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
  while(!outputDone&&g_get_monotonic_time()<deadline){
   if(!inputDone){const ssize_t i=AMediaCodec_dequeueInputBuffer(codec,1000);
    if(i>=0){CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND)&&gst_app_sink_is_eos(GST_APP_SINK(sink)));
     CHECK(AMediaCodec_queueInputBuffer(codec,i,0,0,0,AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)==AMEDIA_OK);inputDone=true;}}
   AMediaCodecBufferInfo info{};const ssize_t o=AMediaCodec_dequeueOutputBuffer(codec,&info,1000);
   if(o>=0){CHECK(info.size<=0);CHECK(AMediaCodec_releaseOutputBuffer(codec,o,false)==AMEDIA_OK);outputDone=info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;}
  }
  CHECK(outputDone);CHECK(gst_app_sink_is_eos(GST_APP_SINK(sink)));avTerminal(pipeline,GST_MESSAGE_EOS);
  CHECK(AMediaCodec_stop(codec)==AMEDIA_OK);AMediaCodec_delete(codec);AImageReader_delete(reader);
  CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
  glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&source);glDeleteTextures(1,&target);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(sink);gst_object_unref(pipeline);
 }
};
#else
struct Decoder {uint64_t fill(relaytest::ReservationOwner&,GstBuffer*,unsigned){CHECK(!"no GPU decoder relay in this build");return 0;}};
#endif
static bool decoderAvailable(){
#ifdef _WIN32
 const char* required[]={"videotestsrc","x264enc","h264parse","d3d11h264dec"};
#elif defined(FL_RELAY_GL_DECODER)
 const char* required[]={"videotestsrc","x264enc","h264parse","nvh264dec","glcolorconvert","glcolorscale"};
 void*cuda=dlopen("libcuda.so.1",RTLD_NOW|RTLD_LOCAL);
 if(!cuda){fprintf(stderr,"SKIP: libcuda.so.1 unavailable (the decoder relay uses NVDEC and CUDA-EGL interop)\n");return false;}
 dlclose(cuda);
#elif defined(FL_RELAY_AMC_DECODER)
 const char* required[]={"videotestsrc","x264enc","h264parse"};
 {std::string name,why;auto*codec=hardwareAvcDecoder(&name,&why);
  if(!codec){fprintf(stderr,"SKIP: %s (the decoder relay needs a hardware MediaCodec decoder)\n",why.c_str());return false;}
  AMediaCodec_delete(codec);}
#elif defined(__ANDROID__)
 fprintf(stderr,"SKIP: built without the decoder fixture plugins (videotestsrc, x264, videoparsersbad)\n");return false;
 const char* required[]={""};
#else
 fprintf(stderr,"SKIP: built without GStreamer GL; the decoder relay needs it\n");return false;
 const char* required[]={""};
#endif
 for(auto name:required){
  auto*factory=gst_element_factory_find(name);if(!factory){fprintf(stderr,"SKIP: required decoder fixture element %s unavailable\n",name);return false;}gst_object_unref(factory);
 }
 return true;
}

static GSocketListener* listener(guint16*port){
 auto*l=g_socket_listener_new();auto*ip=g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
 auto*address=g_inet_socket_address_new(ip,0);GSocketAddress*effective=nullptr;
 CHECK(g_socket_listener_add_address(l,address,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr));
 *port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(effective);g_object_unref(address);g_object_unref(ip);return l;
}
struct Endpoint {
 FlGstSession*session=nullptr;GstElement*pipe=nullptr,*port=nullptr;
 Endpoint(unsigned hop,bool producer,guint16 number,GSocketListener*listen=nullptr,bool decoder=false){
  flSessionConfig cfg{};cfg.version=2;cfg.clockId.bytes[0]=2;cfg.sessionId.bytes[0]=hop+1;cfg.generation=1;cfg.epochNs=5000000;
  cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
  cfg.video={FL_FORMAT_RGBA8,64,32,decoder?30u:1000u,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=5000;
  flSessionCapabilities offer{};offer.version=2;offer.videoCount=offer.audioCount=1;offer.video[0]=cfg.video;offer.audio[0]=cfg.audio;
  session=fl_gst_session_new(3);CHECK(fl_gst_session_configure(session,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_register_track(session,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
  guint8 secret[32]{};secret[0]=73+hop;auto*key=g_bytes_new(secret,32);
  for(unsigned kind=0;kind<3;++kind){
   GSocketConnection*c=nullptr;
   if(listen)c=g_socket_listener_accept(listen,nullptr,nullptr,nullptr);
   else{auto*client=g_socket_client_new();g_socket_client_set_timeout(client,5);c=g_socket_client_connect_to_host(client,"127.0.0.1",number,nullptr,nullptr);g_object_unref(client);}
   CHECK(c);auto policy=nativeAvPolicy(1,true,decoder&&DecoderRenders);
   if(kind==2)CHECK(fl_gst_session_authenticate_native_memory(session,c,producer,key,&policy,5000,nullptr,nullptr));
   else{auto fn=kind?fl_gst_session_authenticate_event_stream:fl_gst_session_authenticate_audio_stream;CHECK(fn(session,c,producer,key,5000,nullptr,nullptr));}
   g_object_unref(c);
  }
  g_bytes_unref(key);
  if(!producer){flClockMapping m{};m.sourceClockId=cfg.clockId;m.targetClockId.bytes[0]=hop?8:7;m.sourceAnchorNs=cfg.epochNs;m.targetAnchorNs=hop?1000000:0;CHECK(fl_gst_session_set_mapping(session,1,&m,0)==FL_GST_SESSION_READY);}
  GError*error=nullptr;
  pipe=gst_parse_launch(producer?"appsrc name=port is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av":"framelinkaudiosrc tracks=av ! appsink name=port max-buffers=2 sync=false async=false enable-last-sample=false",&error);
  CHECK(pipe&&!error);port=gst_bin_get_by_name(GST_BIN(pipe),"port");CHECK(port);auto*context=fl_gst_session_create_context(session);gst_element_set_context(pipe,context);gst_context_unref(context);
 }
 void start(){CHECK(fl_gst_session_track_ready(session,FL_GST_TRACK_VIDEO,1)==FL_GST_SESSION_READY);CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);}
 ~Endpoint(){CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(port);gst_object_unref(pipe);g_object_unref(session);}
};
static void logTo(const std::string&path){CHECK(freopen(path.c_str(),"w",stderr));setvbuf(stderr,nullptr,_IONBF,0);}

static int producer(const std::string&name,guint16 port,Signals&sig,bool empty,bool cancelled=false){
 logTo(name+".producer.log");fprintf(stderr,"producer start\n");
 Endpoint e(0,true,port);fprintf(stderr,"producer authenticated\n");flChannel*writer=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);
 sig[Attached].signal();e.start();
 flBuffer slots[2]{};for(auto&slot:slots)CHECK(flAcquireBuffer(writer,&slot,1000)==FL_OK);unsigned count=empty?0:Frames;
 for(unsigned n=0;n<count;++n){
  auto&input=slots[n%2];flMappedImage pixels{};CHECK(flImageMapPlanes(input.image,&pixels)==FL_OK);
  for(unsigned y=0;y<32;++y)memset(static_cast<char*>(pixels.plane[0].pixels)+y*pixels.plane[0].stride,n+1,256);flImageUnmap(input.image);
  const uint64_t pts=37000000+n*1000000;flFrameMetadata metadata{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(pts-100),1000000,1,{}};
  CHECK(flSetFrameMetadata(writer,&input,&metadata)==FL_OK);CHECK(flSubmit(writer,&input,pts)==FL_OK);
  auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(pcm,0,n+31,192);GST_BUFFER_PTS(pcm)=n*1000000;GST_BUFFER_DURATION(pcm)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(e.port),pcm)==GST_FLOW_OK);
  sig[Held].wait();flBuffer blocked{};CHECK(flAcquireBuffer(writer,&blocked,0)==FL_TIMEOUT);sig[Checked].signal();
  flBuffer released{};CHECK(flAcquireBuffer(writer,&released,5000)==FL_OK&&same(input.image,released.image));input=released;
  if(cancelled)break;
  for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent event{kind,n+1,pts,kind==FL_GST_VIDEO_GAP?123u:0u};CHECK(fl_gst_session_send_video_event(e.session,1,&event)==FL_GST_SESSION_READY);}
 }
 for(auto&slot:slots)CHECK(flCancelBuffer(writer,&slot)==FL_OK);
 if(!cancelled){
  FlGstVideoEvent end{FL_GST_VIDEO_END,count,count?37000000+(count-1)*1000000:0,0};CHECK(fl_gst_session_send_video_event(e.session,1,&end)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_video_eos(e.session,1,count,count?gint64(end.ptsNs):-1)==FL_GST_SESSION_READY);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(e.port))==GST_FLOW_OK);
 }
 sig[Done].wait();
 flClose(writer);return 0;
}
// Actual GstMemory reservations in the source process: neither downstream
// completion nor dropping only the GstBuffer permits reuse of retained memory.
static int reservedProducer(const std::string&name,guint16 port,Signals&sig,bool decoder=false,bool cancelled=false){
 logTo(name+".producer.log");
#ifndef _WIN32
 // NVIDIA's desktop GL context is the CUDA-compatible one; headless EGL.
 if(decoder){g_setenv("GST_GL_API","opengl3",TRUE);g_setenv("GST_GL_PLATFORM","egl",TRUE);g_setenv("GST_GL_WINDOW","surfaceless",TRUE);}
#endif
 Endpoint e(0,true,port,nullptr,decoder);Decoder decoded;flChannel*writer=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);
 auto owner=std::make_shared<relaytest::ReservationOwner>(writer);CHECK(owner->valid());
 sig[Attached].signal();e.start();
 GstBuffer*slots[2]={owner->acquire(1000),owner->acquire(1000)};CHECK(slots[0]&&slots[1]&&!owner->acquire(0));
 for(unsigned n=0;n<Frames;++n){
  auto*&buffer=slots[n%2];auto*memory=gst_buffer_peek_memory(buffer,0);
  auto image=relaytest::reservationOf(buffer)->buffer.image;
  uint64_t hash=0;if(decoder)hash=decoded.fill(*owner,buffer,n);else relaytest::uploadReservation(*owner,buffer,static_cast<unsigned char>(n+1)); // explicit source upload only
  const uint64_t pts=37000000+frameTime(n,decoder);flFrameMetadata metadata{1,decoder?FL_FRAME_HAS_DURATION:FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,decoder?0:int64_t(pts-100),frameDuration(n,decoder),1,{}};
  CHECK(owner->submit(buffer,pts,&metadata)==FL_OK);CHECK(owner->submit(buffer,pts,&metadata)==FL_INVALID);
  const unsigned pcmBytes=decoder?6400:192;auto*pcm=gst_buffer_new_allocate(nullptr,pcmBytes,nullptr);gst_buffer_memset(pcm,0,n+31,pcmBytes);
  if(decoder)CHECK(gst_buffer_fill(pcm,0,&hash,sizeof hash)==sizeof hash);
  GST_BUFFER_PTS(pcm)=frameTime(n,decoder);GST_BUFFER_DURATION(pcm)=decoder?gst_util_uint64_scale(1600,GST_SECOND,48000):1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(e.port),pcm)==GST_FLOW_OK);
  sig[Held].wait();CHECK(!owner->acquire(0));sig[Checked].signal();
  sig[Drained].wait();CHECK(!owner->acquire(0)); // every remote reader released, local GstMemory still held
  auto*retained=gst_memory_ref(memory);gst_buffer_unref(buffer);buffer=nullptr;CHECK(!owner->acquire(0));
  gst_memory_unref(retained);buffer=owner->acquire(1000);CHECK(buffer);
  CHECK(same(image,relaytest::reservationOf(buffer)->buffer.image));
  if(cancelled)break;
  for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent event{kind,n+1,pts,kind==FL_GST_VIDEO_GAP?123u:0u};CHECK(fl_gst_session_send_video_event(e.session,1,&event)==FL_GST_SESSION_READY);}
 }
 for(auto*buffer:slots)gst_buffer_unref(buffer); // cancel the two unused reservations
 if(!cancelled){
  FlGstVideoEvent end{FL_GST_VIDEO_END,Frames,37000000+frameTime(Frames-1,decoder),0};CHECK(fl_gst_session_send_video_event(e.session,1,&end)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_video_eos(e.session,1,Frames,gint64(end.ptsNs))==FL_GST_SESSION_READY);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(e.port))==GST_FLOW_OK);
 }
 sig[Done].wait();
 owner.reset();
 fprintf(stderr,"source GstMemory reservations: remote drain plus final local reference required; duplicate submit rejected; exact allocation reused\n");return 0;
}
static int relay(const std::string&name,guint16 port,relaytest::SharedImages&images,relaytest::Mailbox&notify,Signals&sig,bool empty,bool decoder=false,unsigned fault=0){
 logTo(name+".relay.log");fprintf(stderr,"relay start\n");
 flChannel*up=images.adopt(name+".up",false,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|renderFlag(decoder)),*down=nullptr;
 CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);
 Endpoint output(1,true,port,nullptr,decoder);fprintf(stderr,"relay downstream authenticated\n");guint16 upstreamPort=0;auto*l=listener(&upstreamPort);
 notify.put(upstreamPort);notify.closeWriter();
 Endpoint input(0,false,0,l,decoder);fprintf(stderr,"relay upstream authenticated\n");g_object_unref(l);sig[Attached].wait();
 {
  flsample::SessionNativeRelay video(input.session,output.session,up,down,same);CHECK(video.initialize()==FL_OK);
  flsample::SessionPcmRelay pcm(input.session,output.session,GST_APP_SINK(input.port),GST_APP_SRC(output.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);
  output.start();input.start();unsigned count=empty?0:Frames;
  for(unsigned n=0;n<count;++n){
   flFrame frame{};CHECK(flAcquireFrame(up,&frame,5000)==FL_OK);auto forwarded=video.forward(frame);fprintf(stderr,"frame %u forwarded=%d session=%d\n",n,int(forwarded),int(video.sessionStatus()));CHECK(forwarded==FL_OK);
   auto deadline=g_get_monotonic_time()+5000000;FlGstSessionResult r;
   do{r=pcm.forward(100);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<deadline);}while(r==FL_GST_SESSION_TIMEOUT);
   if(fault){
    const auto began=g_get_monotonic_time();auto result=video.forwardEvent(2000);
    const auto elapsed=g_get_monotonic_time()-began;
    fprintf(stderr,"%s: event result=%d elapsed_us=%lld pending=%u\n",fault==1?"producer death":(fault==3?"downstream death":"downstream cancellation"),int(result),(long long)elapsed,video.pending());
    CHECK(result!=FL_GST_SESSION_READY&&result!=FL_GST_SESSION_TIMEOUT&&elapsed<1500000);
    CHECK(!video.videoEnded()&&video.count()==1&&video.pending()==1);
    if(fault==3){
     // Peer death is NOT consumed-fence completion. Abandon this graph, never
     // synthesize release or recycle an allocation from the dead generation.
     CHECK(video.finish()==FL_DISCONNECTED&&video.pending()==1);
     CHECK(video.collect()==FL_INVALID&&video.pending()==1);
    }else CHECK(video.finish()==FL_BUSY&&video.pending()==1);
    CHECK(video.forward(frame)==FL_INVALID);
    CHECK(pcm.forward(0)!=FL_GST_SESSION_READY);
    CHECK(pcm.blockCount()==1&&pcm.frameCount()==48&&pcm.byteCount()==192);
    sig[FaultChecked].signal();
   }
   if(fault==3)break; // supervisor terminates upstream before graph teardown
   while(video.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(video.collect()==FL_OK);g_usleep(100);}
   if(fault){
    CHECK(video.finish()==FL_OK&&!video.videoEnded());
    if(fault==2)sig[Drained].signal();
    break;
   }
   sig[Drained].signal();
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY);CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY);
  }
  if(!fault){
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY&&video.videoEnded());CHECK(video.finish()==FL_OK);
   avTerminal(input.pipe,GST_MESSAGE_EOS);CHECK(pcm.finish(video,5000)==FL_GST_SESSION_READY);
   CHECK(pcm.frameCount()==count*(decoder?1600:48)&&pcm.byteCount()==count*(decoder?6400:192)&&pcm.blockCount()==count);
  }
  sig[RelayDone].wait();
 }
 flClose(down);flClose(up);return 0;
}
static std::string graphName(const char*kind,const std::string&suffix){
#ifdef _WIN32
 const auto pid=unsigned(GetCurrentProcessId());
#else
 const auto pid=unsigned(getpid());
#endif
 return std::string("gst-relay-")+kind+"-"+std::to_string(pid)+"-"+suffix;
}
static std::vector<relaytest::NativeHandle> inherit(relaytest::SharedImages&images,const relaytest::Mailbox&notify){
 auto handles=images.inherited();handles.push_back(notify.inherited());return handles;
}
static void run(const std::string&plugin,bool empty,bool reserved=false,bool decoder=false,unsigned fault=0){
 relaytest::SharedImages images;Signals sig;
 auto name=graphName("process",std::to_string(empty)+"-"+std::to_string(reserved)+"-"+std::to_string(decoder)+"-"+std::to_string(fault));
 flChannel*down=images.adopt(name+".down",true,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|renderFlag(decoder));
 relaytest::Mailbox notify;guint16 port=0;auto*l=listener(&port);
 relaytest::Child middle({plugin,"--relay-process-child",name,std::to_string(port),images.arg(),notify.arg(),sig.arg(),
  std::to_string(empty),std::to_string(decoder),std::to_string(fault)},sig.all(),inherit(images,notify));
 notify.closeWriter();
 {
  Endpoint receiver(1,false,0,l,decoder);g_object_unref(l);const guint16 upstreamPort=notify.take();
  relaytest::Child first({plugin,decoder?"--relay-decoder-producer":(reserved?"--relay-reserved-producer":"--relay-process-producer"),
   name,std::to_string(upstreamPort),sig.arg(),std::to_string(empty),std::to_string(fault==2)},sig.all());
  receiver.start();
  unsigned count=empty?0:Frames;unsigned seen=0;
  for(unsigned n=0;n<count;++n){
   flFrame frame{};CHECK(flAcquireFrame(down,&frame,5000)==FL_OK&&frame.ptsNs==37000000+frameTime(n,decoder));
   flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&frame,&m)==FL_OK&&(decoder?!(m.flags&FL_FRAME_HAS_DTS):m.dtsNs==int64_t(frame.ptsNs)-100)&&m.durationNs==frameDuration(n,decoder)&&m.streamGeneration==1);
   // The received storage must be one of the original allocations.
   const int which=images.indexOf(frame.image);CHECK(which>=0);seen|=1u<<which;
   uint64_t lease=0;CHECK(flLeaseFrame(down,&frame,&lease)==FL_OK);sig[Held].signal();sig[Checked].wait();
   auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),5*GST_SECOND);CHECK(sample);auto*audio=gst_sample_get_buffer(sample);std::vector<guint8> pcm(decoder?6400:192);gint64 mapped=0;
   CHECK(fl_gst_session_map_running_time(receiver.session,1,frame.ptsNs,TRUE,&mapped)==FL_GST_SESSION_READY);
   CHECK(GST_BUFFER_PTS(audio)==1000000+frameTime(n,decoder)&&mapped-gint64(GST_BUFFER_PTS(audio))==37000000&&GST_BUFFER_DURATION(audio)==(decoder?gst_util_uint64_scale(1600,GST_SECOND,48000):1000000));
   CHECK(gst_buffer_get_size(audio)==pcm.size()&&gst_buffer_extract(audio,0,pcm.data(),pcm.size())==pcm.size());uint64_t expectedHash=0;if(decoder)memcpy(&expectedHash,pcm.data(),sizeof expectedHash);for(size_t i=decoder?8:0;i<pcm.size();++i)CHECK(pcm[i]==n+31);gst_sample_unref(sample);
   if(fault==2){
    // The actual final reader cancels its authenticated session in another
    // process from the relay. Keep its native lease live throughout propagation.
    fl_gst_session_cancel(receiver.session);sig[FaultChecked].wait();
   }else if(fault==1){
    first.terminate();sig[FaultChecked].wait();
   }
   flMappedImage pixels{};CHECK(flImageMapPlanes(frame.image,&pixels)==FL_OK);if(decoder)CHECK(pixelHash(static_cast<unsigned char*>(pixels.plane[0].pixels),pixels.plane[0].stride)==expectedHash);else for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<unsigned char*>(pixels.plane[0].pixels)[y*pixels.plane[0].stride+x]==n+1);flImageUnmap(frame.image);
   CHECK(flCompleteReadLease(down,lease)==FL_OK&&flCollectReadLeases(down)==FL_OK);
   if(fault)break;
   for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent e{};CHECK(fl_gst_session_receive_video_event(receiver.session,1,5000,&e)==FL_GST_SESSION_READY);CHECK(e.kind==kind&&e.afterFrames==n+1&&e.ptsNs==frame.ptsNs&&e.durationNs==(kind==FL_GST_VIDEO_GAP?123:0));}
  }
  if(fault){
   FlGstVideoEvent event{};const auto began=g_get_monotonic_time();
   auto result=fl_gst_session_receive_video_event(receiver.session,1,2000,&event);
   CHECK(result!=FL_GST_SESSION_READY&&result!=FL_GST_SESSION_TIMEOUT&&g_get_monotonic_time()-began<1500000);
   guint64 count=99;gint64 pts=-2;
   CHECK(fl_gst_session_wait_video_eos(receiver.session,1,0,&count,&pts)!=FL_GST_SESSION_READY);
   sig[RelayDone].signal();
   if(fault==2){sig[Done].signal();first.join();}
   middle.join();
   printf("%s (%s): explicit failure, no synthetic END/EOS, held pixels intact, no premature lease completion, bounded native drain\n",fault==1?"producer death":(fault==3?"downstream death":"downstream cancellation"),reserved?"GstMemory":"raw");
  }else{
   CHECK(empty||seen==3);
   FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(receiver.session,1,5000,&end)==FL_GST_SESSION_READY);CHECK(end.kind==FL_GST_VIDEO_END&&end.afterFrames==count&&end.ptsNs==(count?37000000+frameTime(count-1,decoder):0));
   avTerminal(receiver.pipe,GST_MESSAGE_EOS);guint64 finalCount=99;gint64 last=-2;
   CHECK(fl_gst_session_wait_video_eos(receiver.session,1,5000,&finalCount,&last)==FL_GST_SESSION_READY&&finalCount==count&&last==(count?gint64(end.ptsNs):-1));
   CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),0));flFrame extra{};CHECK(flAcquireFrame(down,&extra,0)==FL_TIMEOUT);
   sig[Done].signal();sig[RelayDone].signal();first.join();middle.join();
   printf("three-process coordinated relay: %u same-allocation video frames, %u exact PCM frames; nonempty: both reversed slots / 37ms offset / held-reader exclusion; ordered GAP/barrier/END and authenticated terminal passed\n",count,count*(decoder?1600:48));
  }
 }
 flClose(down);
}

// Separate final reader for actual process-death qualification. The supervisor
// owns the source/relay lifetimes, so killing this process cannot kill the
// other actors or masquerade as graceful cancellation.
static int deathReader(const std::string&name,relaytest::SharedImages&images,relaytest::Mailbox&notify,Signals&sig){
 logTo(name+".reader.log");
 flChannel*down=images.adopt(name+".down",true,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA);
 guint16 port=0;auto*l=listener(&port);notify.put(port);notify.closeWriter();
 Endpoint receiver(1,false,0,l);g_object_unref(l);receiver.start();
 flFrame frame{};CHECK(flAcquireFrame(down,&frame,5000)==FL_OK&&frame.ptsNs==37000000);
 flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&frame,&m)==FL_OK&&m.dtsNs==36999900&&m.durationNs==1000000&&m.streamGeneration==1);
 CHECK(images.indexOf(frame.image)>=0);
 uint64_t lease=0;CHECK(flLeaseFrame(down,&frame,&lease)==FL_OK);
 sig[Held].signal();sig[Checked].wait();
 auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),5*GST_SECOND);CHECK(sample);
 auto*audio=gst_sample_get_buffer(sample);guint8 pcm[192]{};gint64 mapped=0;
 CHECK(fl_gst_session_map_running_time(receiver.session,1,frame.ptsNs,TRUE,&mapped)==FL_GST_SESSION_READY);
 CHECK(GST_BUFFER_PTS(audio)==1000000&&mapped-gint64(GST_BUFFER_PTS(audio))==37000000&&GST_BUFFER_DURATION(audio)==1000000);
 CHECK(gst_buffer_get_size(audio)==192&&gst_buffer_extract(audio,0,pcm,192)==192);for(auto b:pcm)CHECK(b==31);gst_sample_unref(sample);
 flMappedImage pixels{};CHECK(flImageMapPlanes(frame.image,&pixels)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<unsigned char*>(pixels.plane[0].pixels)[y*pixels.plane[0].stride+x]==1);
 flImageUnmap(frame.image);
 fprintf(stderr,"reader verified same allocation, exact pixels/metadata/48 PCM frames, 37ms offset; lease retained; awaiting forced exit\n");
 sig[ReaderReady].signal();
 sig[Never].wait();CHECK(false);return 1;
}
static void downstreamDeath(const std::string&plugin,bool reserved){
 relaytest::SharedImages images;Signals sig;
 auto name=graphName("downstream-death",std::to_string(reserved));
 relaytest::Mailbox readerPort;
 relaytest::Child last({plugin,"--relay-death-reader",name,images.arg(),readerPort.arg(),sig.arg()},sig.all(),inherit(images,readerPort));
 readerPort.closeWriter();const guint16 port=readerPort.take();
 relaytest::Mailbox relayPort;
 relaytest::Child middle({plugin,"--relay-process-child",name,std::to_string(port),images.arg(),relayPort.arg(),sig.arg(),"0","0","3"},
  sig.all(),inherit(images,relayPort));
 relayPort.closeWriter();const guint16 upstream=relayPort.take();
 relaytest::Child first({plugin,reserved?"--relay-reserved-producer":"--relay-process-producer",name,std::to_string(upstream),sig.arg(),"0","0"},sig.all());
 sig[ReaderReady].wait();last.terminate();
 sig[FaultChecked].wait();
 // Source is still fenced out of the dead reader's allocation. Stop the
 // complete old graph before destroying channels/handles; never fake drain.
 CHECK(first.running());first.terminate();
 sig[RelayDone].signal();middle.join();
 printf("downstream process death (%s): reader killed, exact pixels/PCM/37ms offset, bounded failure, lease retained, no synthetic drain; source still fenced then killed, relay exit0; old graph discarded\n",reserved?"GstMemory":"raw");
}

// Child entry points (argv[1] is the plugin, argv[2] the mode).
static bool child(int argc,char**argv,int*code){
 const std::string mode=argv[2];
 if(argc==11&&mode=="--relay-process-child"){
  relaytest::SharedImages images(argv[5]);relaytest::Mailbox notify(argv[6]);Signals sig(argv[7]);
  *code=relay(argv[3],guint16(atoi(argv[4])),images,notify,sig,atoi(argv[8])!=0,atoi(argv[9])!=0,unsigned(atoi(argv[10])));return true;
 }
 if(argc==8&&(mode=="--relay-process-producer"||mode=="--relay-reserved-producer"||mode=="--relay-decoder-producer")){
  Signals sig(argv[5]);const guint16 port=guint16(atoi(argv[4]));const bool empty=atoi(argv[6])!=0,cancelled=atoi(argv[7])!=0;
  if(mode=="--relay-process-producer")*code=producer(argv[3],port,sig,empty,cancelled);
  else *code=reservedProducer(argv[3],port,sig,mode=="--relay-decoder-producer",cancelled);
  return true;
 }
 if(argc==7&&mode=="--relay-death-reader"){
  relaytest::SharedImages images(argv[4]);relaytest::Mailbox notify(argv[5]);Signals sig(argv[6]);
  *code=deathReader(argv[3],images,notify,sig);return true;
 }
 return false;
}
}
