#pragma once
// Platform adapter for the shared native relay tests (fl_gst_native_relay.cpp):
// D3D11 on Windows, DMA-BUF on desktop Linux, AHardwareBuffer on Android (FAR4
// so far: the FAR5 parts below are compiled only with FL_GST_LOCAL_NATIVE_RELAY_POOL). Everything platform-specific in
// those tests lives here: the native memory policy, writing a producer frame,
// one-way control gates between processes, and child processes. The test
// cases themselves are the same on every platform.
#include "../gst/framelink_session_transport.h"
#include "../samples/local_native_relay_setup.h"
#include "../gst/framelink_native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <gst/d3d11/gstd3d11.h>
#include "framelink_d3d11.h"
#include "../samples/native_d3d11_relay_pool.h"
#include "../samples/native_d3d11_relay_transfer.h"
#include "../samples/native_gst_d3d11_relay.h"
#include <dxgi1_3.h>
#include <wrl/client.h>
#elif defined(__ANDROID__)
#include <android/hardware_buffer.h>
#include "framelink_ahb.h"
#include "../samples/native_ahb_relay_pool.h"
#include "../samples/native_ahb_relay_transfer.h"
#include "../samples/native_gst_ahb_relay.h"
#include <dirent.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <gst/allocators/gstdmabuf.h>
#include <cerrno>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include "framelink_dmabuf.h"
#include "../samples/native_dmabuf_relay_pool.h"
#include "../samples/native_dmabuf_relay_transfer.h"
#include "../samples/native_gst_dmabuf_relay.h"
#include <dirent.h>
#include <linux/dma-buf.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <dlfcn.h>
#include <type_traits>
#ifdef FL_RELAY_GL_DECODER
#include <gst/gl/gl.h>
#include <gst/gl/egl/gsteglimage.h>
#endif
#endif
#ifndef CHECK
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#endif

namespace relaytest {

// Producer-ordered local native candidate on this platform's resource. A CPU
// readback receiver unless nativeReceiver; renderProducer adds GPU_RENDER.
inline GstFrameLinkMemoryPolicy nativeAvPolicy(guint64 generation,bool nativeReceiver=false,bool renderProducer=false){
 GstFrameLinkMemoryPolicy policy{};policy.version=1;policy.sessionGeneration=generation;
 for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);
  policy.requirements[i].allowedResourceTypes=FL_GST_LOCAL_NATIVE_RESOURCE;
  policy.requirements[i].allowedOperations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
  policy.paths[i].version=1;policy.paths[i].resourceType=FL_GST_LOCAL_NATIVE_RESOURCE;
 }
 policy.paths[0].supportedUsage=policy.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE;
 policy.paths[1].supportedUsage=policy.requirements[1].requiredUsage=FL_MEMORY_CPU_READ;
 policy.paths[1].operations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
 if(nativeReceiver){
  for(unsigned i=0;i<2;++i){policy.requirements[i].allowedOperations=0;
   policy.paths[i].operations=0;
   policy.paths[i].supportedUsage=policy.requirements[i].requiredUsage=FL_MEMORY_GPU_SAMPLE;}
 }
 if(renderProducer)policy.paths[0].supportedUsage=policy.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE|FL_MEMORY_GPU_RENDER;
 return policy;
}

// The relay fixtures' media: RGBA 64x32 at 30 fps and S16 stereo 48 kHz.
inline flSessionCapabilities localCaps(){
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=caps.audioCount=1;
 caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};caps.audio[0]={FL_PCM_S16,48000,2};return caps;
}

// Write one byte value into every pixel of an acquired 64x32 RGBA buffer the
// way the platform's producer does: a D3D11 upload, or a mapped DMA-BUF.
inline void fill(flChannel*channel,const flBuffer&buffer,unsigned char value){
#ifdef _WIN32
 ID3D11DeviceContext*context=nullptr;flChannelD3D11Device(channel)->GetImmediateContext(&context);CHECK(context);
 std::vector<unsigned char> pixels(64*32*4,value);
 context->UpdateSubresource(flImageD3D11(buffer.image),0,nullptr,pixels.data(),256,0);
 context->Release();
#else
 (void)channel;flMappedImage map{};CHECK(flImageMapPlanes(buffer.image,&map)==FL_OK);
 for(unsigned y=0;y<32;++y)std::memset(static_cast<char*>(map.plane[0].pixels)+y*map.plane[0].stride,value,256);
 flImageUnmap(buffer.image);
#endif
}

#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
// A FAR5 sender's borrowed pool on this platform: D3D11 shared handles plus
// the adapter LUID on Windows, DMA-BUF images on Linux.
struct PoolExport {
#ifdef _WIN32
 guint8 adapter[8]{};void*handles[FL_MAX_POOL_SIZE]{};
#elif defined(__ANDROID__)
 AHardwareBuffer*buffers[FL_MAX_POOL_SIZE]{};
#else
 flDmabuf images[FL_MAX_POOL_SIZE]{};
#endif
};
// Export one unpublished producer reservation (or source image) into slot.
inline void exportImage(flChannel*channel,flImage*image,PoolExport&pool,unsigned slot){
#ifdef _WIN32
 flChannelInfo info{};CHECK(flQuery(channel,&info)==FL_OK);memcpy(pool.adapter,info.adapterLUID,8);
 pool.handles[slot]=flImageSharedHandle(image);CHECK(pool.handles[slot]);
#elif defined(__ANDROID__)
 (void)channel;pool.buffers[slot]=flImageAHardwareBuffer(image);CHECK(pool.buffers[slot]);
#else
 (void)channel;CHECK(flImageDmabuf(image,&pool.images[slot])==FL_OK);
#endif
}
// The platform's typed FAR5 sender.
inline gboolean openRelayPool(FlGstSession*down,FlGstSession*up,const char*path,const flSessionCapabilities*caps,
 guint queue,guint pool,guint audio,guint timeout,guint idle,const GstFrameLinkMemoryPolicy*candidate,
 const PoolExport&exports,GstFrameLinkMemoryPolicy*bound,flChannel**channel,GCancellable*cancel,GError**error){
#ifdef _WIN32
 return fl_gst_session_open_local_native_relay_pool(down,up,path,caps,queue,pool,audio,timeout,idle,candidate,
  exports.adapter,exports.handles,bound,channel,cancel,error);
#elif defined(__ANDROID__)
 return fl_gst_session_open_local_native_relay_pool_ahb(down,up,path,caps,queue,pool,audio,timeout,idle,candidate,
  exports.buffers,bound,channel,cancel,error);
#else
 return fl_gst_session_open_local_native_relay_pool_dmabuf(down,up,path,caps,queue,pool,audio,timeout,idle,candidate,
  exports.images,bound,channel,cancel,error);
#endif
}
#endif
// Same allocation, by kernel object identity - never slot index or size.
inline bool sameAllocation(flImage*a,flImage*b){
#ifdef _WIN32
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 static auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));
 CHECK(compare);return compare(flImageSharedHandle(a),flImageSharedHandle(b))!=FALSE;
#elif defined(__ANDROID__)
 // The AHardwareBuffer allocation ID (API 31+), unique per allocation.
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
 static auto getId=reinterpret_cast<GetId>(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));CHECK(getId);
 uint64_t x=0,y=0;CHECK(getId(flImageAHardwareBuffer(a),&x)==0&&getId(flImageAHardwareBuffer(b),&y)==0);return x==y;
#else
 flDmabuf x{},y{};CHECK(flImageDmabuf(a,&x)==FL_OK&&flImageDmabuf(b,&y)==FL_OK);
 struct stat sx{},sy{};CHECK(fstat(x.fd[0],&sx)==0&&fstat(y.fd[0],&sy)==0);
 return sx.st_dev==sy.st_dev&&sx.st_ino==sy.st_ino;
#endif
}
#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
// GstMemory of this platform's native resource (D3D11Memory, DMABuf, or the
// FrameLink AHardwareBuffer reader memory).
inline bool isNativeMemory(GstMemory*memory){
#ifdef _WIN32
 return gst_is_d3d11_memory(memory);
#elif defined(__ANDROID__)
 return gst_framelink_memory_get_hardware_buffer(memory)!=nullptr;
#else
 return gst_is_dmabuf_memory(memory);
#endif
}
// The first byte of a native frame (a mapped read, or a locked AHardwareBuffer).
inline guint8 nativeFirstByte(GstMemory*memory){
#ifdef __ANDROID__
 auto*b=gst_framelink_memory_get_hardware_buffer(memory);CHECK(b);void*data=nullptr;
 CHECK(AHardwareBuffer_lock(b,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,nullptr,&data)==0&&data);
 const guint8 value=*static_cast<const guint8*>(data);CHECK(AHardwareBuffer_unlock(b,nullptr)==0);return value;
#else
 GstMapInfo map{};CHECK(gst_memory_map(memory,&map,GST_MAP_READ));const guint8 value=map.data[0];gst_memory_unmap(memory,&map);return value;
#endif
}
// Every byte of a 64x32 RGBA native frame equals value. The memory is mapped
// through GStreamer; Android's AHardwareBuffer reader memory is deliberately
// not CPU-mappable, so it is locked directly (offset/stride are unused there).
inline void checkNativePixels(GstMemory*memory,gsize offset,gint stride,guint8 value){
#ifdef __ANDROID__
 (void)offset;(void)stride;auto*b=gst_framelink_memory_get_hardware_buffer(memory);CHECK(b);
 AHardwareBuffer_Desc d{};AHardwareBuffer_describe(b,&d);void*data=nullptr;
 CHECK(AHardwareBuffer_lock(b,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,nullptr,&data)==0&&data);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<const guint8*>(data)[size_t(y)*d.stride*4+x]==value);
 CHECK(AHardwareBuffer_unlock(b,nullptr)==0);
#else
 GstMapInfo map{};CHECK(gst_memory_map(memory,&map,GST_MAP_READ));
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(map.data[offset+y*stride+x]==value);
 gst_memory_unmap(memory,&map);
#endif
}
// Identity of one snapshot slot's allocation, comparable across graphs while
// both snapshots live: the borrowed shared handle, or the DMA-BUF inode.
struct AllocationId {
#ifdef _WIN32
 void*handle=nullptr;
#elif defined(__ANDROID__)
 uint64_t id=0;
#else
 dev_t device=0;ino_t inode=0;
#endif
};
inline AllocationId snapshotAllocation(const flsample::LocalPoolSnapshot*snapshot,unsigned slot){
 AllocationId id{};
#ifdef _WIN32
 id.handle=flD3D11PoolSnapshotHandle(snapshot,slot);CHECK(id.handle);
#elif defined(__ANDROID__)
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
 static auto getId=reinterpret_cast<GetId>(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));CHECK(getId);
 auto*b=flAHardwareBufferPoolSnapshotBuffer(snapshot,slot);CHECK(b&&getId(b,&id.id)==0&&id.id);
#else
 flDmabuf d{};CHECK(flDmabufPoolSnapshotImage(snapshot,slot,&d)==FL_OK);
 struct stat st{};CHECK(fstat(d.fd[0],&st)==0);id.device=st.st_dev;id.inode=st.st_ino;
#endif
 return id;
}
inline bool sameAllocation(const AllocationId&a,const AllocationId&b){
#ifdef _WIN32
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 static auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));
 CHECK(compare);return compare(a.handle,b.handle)!=FALSE;
#elif defined(__ANDROID__)
 return a.id==b.id;
#else
 return a.device==b.device&&a.inode==b.inode;
#endif
}
// Malformed exports for the FAR5 failure controls. Foreign is a valid kernel
// object that is not an image (an event, or a pipe). Mismatch is the case each
// platform can produce: a wrong adapter LUID on Windows; on Linux, where a
// second GPU is not available to the test, a malformed plane layout.
enum class BadExport {Duplicate,Invalid,Foreign,Mismatch};
// Every exported handle or fd is still open (failures never close the caller's).
inline bool exportsAlive(const PoolExport&pool,unsigned count){
 for(unsigned n=0;n<count;++n){
#ifdef _WIN32
  DWORD flags=0;if(!GetHandleInformation(pool.handles[n],&flags))return false;
#elif defined(__ANDROID__)
  // Borrowed references: the export must still name a live buffer.
  if(!pool.buffers[n])return false;AHardwareBuffer_Desc d{};AHardwareBuffer_describe(pool.buffers[n],&d);if(!d.width)return false;
#else
  for(uint32_t p=0;p<pool.images[n].planes;++p)if(fcntl(pool.images[n].fd[p],F_GETFD)==-1)return false;
#endif
 }
 return true;
}

#endif
// One-way control signal between this process and a spawned child. Each
// signal() is consumed by exactly one wait(), bounded at 10 s. A child gets
// the gate from arg() on its command line; either side may signal or wait.
struct Gate {
#ifdef _WIN32
 HANDLE event=nullptr;
 Gate(){event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);}
 explicit Gate(const char*arg){event=reinterpret_cast<HANDLE>(uintptr_t(strtoull(arg,nullptr,10)));CHECK(event);}
 ~Gate(){if(event)CloseHandle(event);}
 void signal(){CHECK(SetEvent(event));}
 void wait(){CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);}
 bool idle(){return WaitForSingleObject(event,0)==WAIT_TIMEOUT;} // no signal pending
 std::string arg()const{return std::to_string(uintptr_t(event));}
 std::vector<HANDLE> inherited()const{return {event};}
#else
 int fd[2]{-1,-1};
 Gate(){CHECK(pipe2(fd,O_CLOEXEC)==0);}
 explicit Gate(const char*arg){CHECK(sscanf(arg,"%d,%d",&fd[0],&fd[1])==2&&fd[0]>=0&&fd[1]>=0);}
 ~Gate(){for(int f:fd)if(f>=0)close(f);}
 void signal(){const char token=1;ssize_t n;do n=write(fd[1],&token,1);while(n<0&&errno==EINTR);CHECK(n==1);}
 void wait(){
  pollfd p{fd[0],POLLIN,0};int ready;do ready=poll(&p,1,10000);while(ready<0&&errno==EINTR);CHECK(ready==1);
  char token=0;ssize_t n;do n=read(fd[0],&token,1);while(n<0&&errno==EINTR);CHECK(n==1&&token==1);
 }
 bool idle(){pollfd p{fd[0],POLLIN,0};return poll(&p,1,0)==0;} // no signal pending
 std::string arg()const{return std::to_string(fd[0])+","+std::to_string(fd[1]);}
 std::vector<int> inherited()const{return {fd[0],fd[1]};}
#endif
 Gate(const Gate&)=delete;Gate&operator=(const Gate&)=delete;
};

#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
inline void corrupt(PoolExport&pool,BadExport kind,const Gate&foreign){
#ifdef _WIN32
 if(kind==BadExport::Duplicate)pool.handles[1]=pool.handles[0];
 if(kind==BadExport::Invalid)pool.handles[1]=reinterpret_cast<void*>(~uintptr_t(0));
 if(kind==BadExport::Foreign)pool.handles[1]=foreign.event;
 if(kind==BadExport::Mismatch)memset(pool.adapter,0xff,8);
#elif defined(__ANDROID__)
 // Foreign: a real AHardwareBuffer that is not one of the ring's (wrong size).
 // Mismatch: a ring-sized buffer in another format, rejected at adoption.
 (void)foreign;
 auto make=[](uint32_t w,uint32_t h,uint32_t format){AHardwareBuffer_Desc d{};d.width=w;d.height=h;d.layers=1;d.format=format;
  d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
  AHardwareBuffer*b=nullptr;CHECK(AHardwareBuffer_allocate(&d,&b)==0&&b);return b;};
 static AHardwareBuffer*foreignBuffer=make(16,16,AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
 static AHardwareBuffer*otherFormat=make(64,32,AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM);
 if(kind==BadExport::Duplicate)pool.buffers[1]=pool.buffers[0];
 if(kind==BadExport::Invalid)pool.buffers[1]=nullptr;
 if(kind==BadExport::Foreign)pool.buffers[1]=foreignBuffer;
 if(kind==BadExport::Mismatch)pool.buffers[1]=otherFormat;
#else
 if(kind==BadExport::Duplicate)pool.images[1]=pool.images[0];
 if(kind==BadExport::Invalid)pool.images[1].fd[0]=-1;
 if(kind==BadExport::Foreign)pool.images[1].fd[0]=foreign.fd[0];
 if(kind==BadExport::Mismatch)pool.images[1].stride[0]=0;
#endif
}

#endif
#ifdef _WIN32
using NativeHandle=HANDLE;
#else
using NativeHandle=int;
#endif

// A child process running this same test executable with the given arguments.
// It inherits only the listed gates and `extra` native handles (a broker's
// allocations, a mailbox) and dies with the parent. join() requires a clean
// exit within 10 s.
struct Child {
#ifdef _WIN32
 PROCESS_INFORMATION process{};
 Child(const std::vector<std::string>&args,const std::vector<const Gate*>&gates,const std::vector<NativeHandle>&extra={}){
  static HANDLE job=nullptr;
  if(!job){job=CreateJobObjectA(nullptr,nullptr);CHECK(job);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
   limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
   CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits));}
  char exe[MAX_PATH]{};CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));
  std::string command="\""+std::string(exe)+"\"";for(auto&a:args)command+=" \""+a+"\"";
  std::vector<HANDLE> handles(extra);for(auto*g:gates)for(auto h:g->inherited())handles.push_back(h);
  STARTUPINFOEXA startup{};startup.StartupInfo.cb=sizeof startup;SIZE_T bytes=0;
  InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> storage(bytes);
  startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  CHECK(InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes));
  for(auto h:handles)CHECK(SetHandleInformation(h,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT));
  CHECK(UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles.data(),handles.size()*sizeof(HANDLE),nullptr,nullptr));
  CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,
   nullptr,nullptr,&startup.StartupInfo,&process));
  CHECK(AssignProcessToJobObject(job,process.hProcess));CHECK(ResumeThread(process.hThread)!=DWORD(-1));
  for(auto h:handles)CHECK(SetHandleInformation(h,HANDLE_FLAG_INHERIT,0));
  DeleteProcThreadAttributeList(startup.lpAttributeList);
 }
 void join(){
  CHECK(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);DWORD code=1;
  CHECK(GetExitCodeProcess(process.hProcess,&code)&&code==0);
  CloseHandle(process.hThread);CloseHandle(process.hProcess);process={};
 }
 // Abrupt death (no cleanup in the child), confirmed.
 void terminate(){
  CHECK(TerminateProcess(process.hProcess,73));CHECK(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
  DWORD code=0;CHECK(GetExitCodeProcess(process.hProcess,&code)&&code==73);
  CloseHandle(process.hThread);CloseHandle(process.hProcess);process={};
 }
 bool running(){return WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT;}
 ~Child(){if(process.hProcess){TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);}}
#else
 pid_t pid=-1;
 Child(const std::vector<std::string>&args,const std::vector<const Gate*>&gates,const std::vector<NativeHandle>&extra={}){
  std::vector<int> fds(extra);for(auto*g:gates)for(int f:g->inherited())fds.push_back(f);
  const pid_t parent=getpid();
  pid=fork();CHECK(pid>=0);
  if(!pid){
   prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(70); // dies with the parent
   for(int f:fds)if(fcntl(f,F_SETFD,0)!=0)_exit(71);
   std::vector<char*> argv;static char self[]="/proc/self/exe";argv.push_back(self);
   for(auto&a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
   execv(self,argv.data());_exit(72);
  }
 }
 void join(){
  int status=0;pid_t done=0;
  for(int n=0;n<1000&&!(done=waitpid(pid,&status,WNOHANG));n++)std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(done==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);pid=-1;
 }
 // Abrupt death (no cleanup in the child), confirmed.
 void terminate(){
  CHECK(kill(pid,SIGKILL)==0);int status=0;CHECK(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);pid=-1;
 }
 bool running(){int status=0;return waitpid(pid,&status,WNOHANG)==0;}
 ~Child(){if(pid>0){kill(pid,SIGKILL);waitpid(pid,nullptr,0);}}
#endif
 Child(const Child&)=delete;Child&operator=(const Child&)=delete;
};


// Android: phase 4 of its relay parity work.
#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
// ---- In-process GStreamer bin relays ----

// The native producer pool's caps feature, and the completion contract the
// producer offers in its allocation query.
inline const char*nativeFeature(){
#ifdef _WIN32
 return "memory:D3D11Memory";
#elif defined(__ANDROID__)
 return "memory:FrameLinkAHardwareBuffer";
#else
 return "memory:DMABuf";
#endif
}
inline const char*producerCompletion(){
#ifdef _WIN32
 return "same-device-before-abandon-v1";
#else
 return "external-sync-fd-before-submit-v1";
#endif
}
// Write one byte value into every pixel of a 64x32 RGBA buffer from the native
// sink's own pool, and complete it for submission: a D3D11 upload, or a mapped
// DMA-BUF write followed by a signalled ready fd given to the sink.
inline void fillPoolBuffer(GstElement*sink,GstBuffer*b,unsigned char value){
 auto*mem=gst_buffer_peek_memory(b,0);
#ifdef _WIN32
 (void)sink;CHECK(gst_is_d3d11_memory(mem));
 auto*reservation=(FrameLinkD3DReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(mem),FrameLinkD3DProducer::key());CHECK(reservation);
 auto*texture=(ID3D11Texture2D*)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem));CHECK(texture==flImageD3D11(reservation->buffer.image));
 ID3D11Device*d=nullptr;texture->GetDevice(&d);ID3D11DeviceContext*c=nullptr;d->GetImmediateContext(&c);
 std::vector<unsigned char>pixels(64*32*4,value);c->UpdateSubresource(texture,0,nullptr,pixels.data(),256,0);
 c->Release();d->Release();
#elif defined(__ANDROID__)
 // A locked AHardwareBuffer write, then a signalled ready fd for the sink.
 auto*ahb=gst_framelink_memory_get_hardware_buffer(mem);CHECK(ahb);
 CHECK(gst_mini_object_get_qdata(GST_MINI_OBJECT(mem),FrameLinkAhbProducer::key()));
 AHardwareBuffer_Desc d{};AHardwareBuffer_describe(ahb,&d);void*data=nullptr;
 CHECK(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data)==0&&data);
 for(unsigned y=0;y<32;++y)memset(static_cast<char*>(data)+size_t(y)*d.stride*4,value,256);
 CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0); // CPU writes complete and visible
 const int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);
 uint64_t one=1;CHECK(write(ready,&one,sizeof one)==sizeof one);
 gboolean accepted=FALSE;g_signal_emit_by_name(sink,"set-native-ready-fd",b,ready,&accepted);CHECK(accepted);
 close(ready);
#else
 CHECK(gst_is_dmabuf_memory(mem));const int fd=gst_dmabuf_memory_get_fd(mem);
 auto*layout=gst_buffer_get_video_meta(b);CHECK(layout);
 dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 void*data=mmap(nullptr,mem->size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(data!=MAP_FAILED);
 for(unsigned y=0;y<32;++y)memset(static_cast<char*>(data)+layout->offset[0]+y*layout->stride[0],value,256);
 CHECK(munmap(data,mem->size)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 const int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);
 uint64_t one=1;CHECK(write(ready,&one,sizeof one)==sizeof one); // CPU writes are complete
 gboolean accepted=FALSE;g_signal_emit_by_name(sink,"set-native-ready-fd",b,ready,&accepted);CHECK(accepted);
 close(ready); // the sink keeps its own duplicate
#endif
}
// An intact FrameLink native reader memory (not producer or ordinary memory).
inline bool isReaderMemory(GstMemory*m){
#ifdef _WIN32
 return gst_framelink_memory_get_d3d11_shared_handle(m)!=nullptr;
#elif defined(__ANDROID__)
 // FrameLink AHardwareBuffer memory that is not a producer reservation.
 return gst_framelink_memory_get_hardware_buffer(m)!=nullptr&&!gst_mini_object_get_qdata(GST_MINI_OBJECT(m),FrameLinkAhbProducer::key());
#else
 return gst_framelink_memory_get_dmabuf_fd(m)>=0;
#endif
}
// Two FrameLink reader memories name the same allocation (kernel identity).
inline bool sameMemory(GstMemory*a,GstMemory*b){
#ifdef _WIN32
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 static auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));
 CHECK(compare);auto x=gst_framelink_memory_get_d3d11_shared_handle(a),y=gst_framelink_memory_get_d3d11_shared_handle(b);
 CHECK(x&&y);return compare(x,y)!=FALSE;
#elif defined(__ANDROID__)
 const uint64_t x=flsample::ahbAllocationId(gst_framelink_memory_get_hardware_buffer(a)),y=flsample::ahbAllocationId(gst_framelink_memory_get_hardware_buffer(b));
 CHECK(x&&y);return x==y;
#else
 const int x=gst_framelink_memory_get_dmabuf_fd(a),y=gst_framelink_memory_get_dmabuf_fd(b);CHECK(x>=0&&y>=0);
 struct stat sx{},sy{};CHECK(fstat(x,&sx)==0&&fstat(y,&sy)==0);return sx.st_dev==sy.st_dev&&sx.st_ino==sy.st_ino;
#endif
}
// Identity of a native producer-pool memory, stable while it is retained.
inline uint64_t poolAllocation(GstMemory*m){
#ifdef _WIN32
 CHECK(gst_is_d3d11_memory(m));return uint64_t(uintptr_t(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(m))));
#elif defined(__ANDROID__)
 const uint64_t id=flsample::ahbAllocationId(gst_framelink_memory_get_hardware_buffer(m));CHECK(id);return id;
#else
 CHECK(gst_is_dmabuf_memory(m));struct stat st{};CHECK(fstat(gst_dmabuf_memory_get_fd(m),&st)==0);return uint64_t(st.st_ino);
#endif
}
// A retained native memory still names a live resource after every pipeline,
// pool and session is gone: its D3D11 device is not removed, or its fd is open.
inline void checkRetained(GstMemory*m){
#ifdef _WIN32
 CHECK(gst_is_d3d11_memory(m));auto*resource=gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(m));CHECK(resource);
 ID3D11Device*device=nullptr;resource->GetDevice(&device);CHECK(device&&SUCCEEDED(device->GetDeviceRemovedReason()));device->Release();
#elif defined(__ANDROID__)
 auto*b=gst_framelink_memory_get_hardware_buffer(m);CHECK(b);AHardwareBuffer_Desc d{};AHardwareBuffer_describe(b,&d);CHECK(d.width==64&&d.height==32);
#else
 CHECK(gst_is_dmabuf_memory(m));struct stat st{};CHECK(fstat(gst_dmabuf_memory_get_fd(m),&st)==0);
#endif
}
// Open kernel handles (Windows) or file descriptors (Linux) of this process.
inline unsigned long openHandles(){
#ifdef _WIN32
 DWORD handles=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles));return handles;
#else
 unsigned long count=0;DIR*d=opendir("/proc/self/fd");CHECK(d);
 while(auto*e=readdir(d))if(e->d_name[0]!='.')++count;
 closedir(d);return count-1; // the directory stream's own fd
#endif
}
// The platform's relay samples: pool export guard, in-process same-allocation
// ring, and the GStreamer read-lease adapter.
#ifdef _WIN32
using RelayPoolExport=flsample::NativeD3D11RelayPoolExport;
using GstRelay=flsample::NativeGstD3D11Relay;
inline flResult createRelayPool(GstBufferPool*pool,const char*name,const flSessionConfig&config,unsigned capacity,bool cpu,flChannel**reader,flChannel**writer){
 return flsample::createNativeD3D11RelayPool(pool,name,config,capacity,cpu,reader,writer);}
#elif defined(__ANDROID__)
using RelayPoolExport=flsample::NativeAhbRelayPoolExport;
using GstRelay=flsample::NativeGstAhbRelay;
inline flResult createRelayPool(GstBufferPool*pool,const char*name,const flSessionConfig&config,unsigned capacity,bool cpu,flChannel**reader,flChannel**writer){
 return flsample::createNativeAhbRelayPool(pool,name,config,capacity,cpu,reader,writer);}
#else
using RelayPoolExport=flsample::NativeDmabufRelayPoolExport;
using GstRelay=flsample::NativeGstDmabufRelay;
inline flResult createRelayPool(GstBufferPool*pool,const char*name,const flSessionConfig&config,unsigned capacity,bool cpu,flChannel**reader,flChannel**writer){
 return flsample::createNativeDmabufRelayPool(pool,name,config,capacity,cpu,reader,writer);}
#endif
// The typed FAR5 sender over a pinned producer pool.
inline gboolean openRelayPool(FlGstSession*down,FlGstSession*up,const char*path,const flSessionCapabilities*caps,
 guint queue,guint pool,guint audio,guint timeout,guint idle,const GstFrameLinkMemoryPolicy*candidate,
 const RelayPoolExport&exported,GstFrameLinkMemoryPolicy*bound,flChannel**channel,GCancellable*cancel,GError**error){
#ifdef _WIN32
 return fl_gst_session_open_local_native_relay_pool(down,up,path,caps,queue,pool,audio,timeout,idle,candidate,
  exported.first.adapterLUID,exported.handles.data(),bound,channel,cancel,error);
#elif defined(__ANDROID__)
 return fl_gst_session_open_local_native_relay_pool_ahb(down,up,path,caps,queue,pool,audio,timeout,idle,candidate,
  exported.images.data(),bound,channel,cancel,error);
#else
 return fl_gst_session_open_local_native_relay_pool_dmabuf(down,up,path,caps,queue,pool,audio,timeout,idle,candidate,
  exported.images.data(),bound,channel,cancel,error);
#endif
}
// Cancellation for the broker-keyed transfer: a manual-reset event, or a GCancellable.
struct Cancel {
#ifdef _WIN32
 HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Cancel(){CHECK(event);}
 ~Cancel(){CloseHandle(event);}
 void set(){CHECK(SetEvent(event));}
 void reset(){CHECK(ResetEvent(event));}
#else
 GCancellable*cancellable=g_cancellable_new();
 ~Cancel(){g_object_unref(cancellable);}
 void set(){g_cancellable_cancel(cancellable);}
 void reset(){g_cancellable_reset(cancellable);}
 Cancel()=default;
#endif
 Cancel(const Cancel&)=delete;Cancel&operator=(const Cancel&)=delete;
};
// Broker-keyed same-allocation transfer (explicit endpoint and secret, not
// session discovery): a current-user named pipe, or an abstract Unix socket.
inline flResult createAuthenticatedRelayPool(bool producer,GstBufferPool*pool,const std::string&endpoint,const char*ring,
 const flSessionConfig&config,unsigned capacity,bool cpu,GBytes*key,Cancel&cancel,guint timeout,flChannel**out){
#ifdef _WIN32
 const std::wstring pipe=L"\\\\.\\pipe\\"+std::wstring(endpoint.begin(),endpoint.end());
 return flsample::createAuthenticatedNativeD3D11RelayPool(producer,pool,pipe,ring,config,capacity,cpu,key,cancel.event,timeout,out);
#elif defined(__ANDROID__)
 return flsample::createAuthenticatedNativeAhbRelayPool(producer,pool,std::string(1,'\0')+endpoint,ring,config,capacity,cpu,key,cancel.cancellable,timeout,out);
#else
 return flsample::createAuthenticatedNativeDmabufRelayPool(producer,pool,std::string(1,'\0')+endpoint,ring,config,capacity,cpu,key,cancel.cancellable,timeout,out);
#endif
}


// ---- Broker-composed process relays ----

// One value (a listening port) written by a child, read by its parent. The
// child inherits only the write end.
struct Mailbox {
#ifdef _WIN32
 HANDLE read=nullptr,write=nullptr;
 Mailbox(){CHECK(CreatePipe(&read,&write,nullptr,0));}
 explicit Mailbox(const char*arg){write=reinterpret_cast<HANDLE>(uintptr_t(strtoull(arg,nullptr,10)));CHECK(write);}
 ~Mailbox(){if(read)CloseHandle(read);if(write)CloseHandle(write);}
 void put(guint16 value){DWORD n=0;CHECK(WriteFile(write,&value,sizeof value,&n,nullptr)&&n==sizeof value);}
 guint16 take(){guint16 value=0;DWORD n=0;CHECK(ReadFile(read,&value,sizeof value,&n,nullptr)&&n==sizeof value);return value;}
 void closeWriter(){if(write)CloseHandle(write);write=nullptr;}
 std::string arg()const{return std::to_string(uintptr_t(write));}
 NativeHandle inherited()const{return write;}
#else
 int read=-1,write=-1;
 Mailbox(){int fd[2];CHECK(pipe2(fd,O_CLOEXEC)==0);read=fd[0];write=fd[1];}
 explicit Mailbox(const char*arg){write=atoi(arg);CHECK(write>=0);}
 ~Mailbox(){if(read>=0)close(read);if(write>=0)close(write);}
 void put(guint16 value){ssize_t n;do n=::write(write,&value,sizeof value);while(n<0&&errno==EINTR);CHECK(n==sizeof value);}
 guint16 take(){
  pollfd p{read,POLLIN,0};int ready;do ready=poll(&p,1,10000);while(ready<0&&errno==EINTR);CHECK(ready==1);
  guint16 value=0;ssize_t n;do n=::read(read,&value,sizeof value);while(n<0&&errno==EINTR);CHECK(n==sizeof value);return value;
 }
 void closeWriter(){if(write>=0)close(write);write=-1;}
 std::string arg()const{return std::to_string(write);}
 NativeHandle inherited()const{return write;}
#endif
 Mailbox(const Mailbox&)=delete;Mailbox&operator=(const Mailbox&)=delete;
};

// Two 64x32 RGBA allocations a broker makes once and hands, as native handles,
// to every ring of a graph: D3D11 shared NT handles, or linear GBM DMA-BUFs on
// the render node FrameLink uses. A child reconstructs them from arg().
struct SharedImages {
#ifdef _WIN32
 Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[2];HANDLE handles[2]{};
 SharedImages(){
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
  D3D11_TEXTURE2D_DESC d{};d.Width=64;d.Height=32;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
  d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
  for(unsigned i=0;i<2;++i){CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&textures[i])));Microsoft::WRL::ComPtr<IDXGIResource1> resource;
   CHECK(SUCCEEDED(textures[i].As(&resource)));
   CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handles[i])));}
 }
 explicit SharedImages(const char*arg){
  unsigned long long a=0,b=0;CHECK(sscanf(arg,"%llu,%llu",&a,&b)==2);
  handles[0]=reinterpret_cast<HANDLE>(uintptr_t(a));handles[1]=reinterpret_cast<HANDLE>(uintptr_t(b));CHECK(handles[0]&&handles[1]);
 }
 ~SharedImages(){for(auto h:handles)if(h)CloseHandle(h);}
 std::string arg()const{return std::to_string(uintptr_t(handles[0]))+","+std::to_string(uintptr_t(handles[1]));}
 std::vector<NativeHandle> inherited()const{return {handles[0],handles[1]};}
 flChannel* adopt(const std::string&name,bool reversed,uint32_t flags)const{
  void*h[2]={handles[reversed?1:0],handles[reversed?0:1]};flChannel*ch=nullptr;
  CHECK(flCreateChannelFromD3D11Handles(name.c_str(),64,32,FL_FORMAT_RGBA8,2,flags,nullptr,h,&ch)==FL_OK);return ch;
 }
 // Which original allocation an image of an adopting ring is (-1: neither).
 int indexOf(flImage*image)const{
  using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
  static auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));CHECK(compare);
  for(int i=0;i<2;++i)if(compare(flImageSharedHandle(image),handles[i]))return i;
  return -1;
 }
#elif defined(__ANDROID__)
 // AHardwareBuffers cannot be inherited like fds: each child gets its own
 // socket pair, into which both buffers are sent before it starts (queued
 // messages stay readable after this end closes). arg() and inherited() name
 // the same pending child end; the next child gets a fresh pair.
 AHardwareBuffer*buffers[2]{};int pending=-1;unsigned pendingUses=0;
 static AHardwareBuffer*allocate(){
  AHardwareBuffer_Desc d{};d.width=64;d.height=32;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  // GPU_COLOR_OUTPUT: the decoder graph renders into these (FL_GPU_RENDER rings).
  d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
  AHardwareBuffer*b=nullptr;CHECK(AHardwareBuffer_allocate(&d,&b)==0&&b);return b;
 }
 SharedImages(){for(auto&b:buffers)b=allocate();}
 explicit SharedImages(const char*arg){
  const int fd=atoi(arg);CHECK(fd>=0);CHECK(fcntl(fd,F_SETFD,FD_CLOEXEC)==0);
  for(auto&b:buffers){pollfd p{fd,POLLIN,0};CHECK(poll(&p,1,10000)==1);CHECK(AHardwareBuffer_recvHandleFromUnixSocket(fd,&b)==0&&b);}
  close(fd);
 }
 ~SharedImages(){for(auto*b:buffers)if(b)AHardwareBuffer_release(b);if(pending>=0)close(pending);}
 void prepare(){
  if(pending>=0&&pendingUses<2){++pendingUses;return;}
  if(pending>=0)close(pending);
  int pair[2];CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);
  for(auto*b:buffers)CHECK(AHardwareBuffer_sendHandleToUnixSocket(b,pair[0])==0);
  close(pair[0]);pending=pair[1];pendingUses=1;
 }
 std::string arg(){prepare();return std::to_string(pending);}
 std::vector<NativeHandle> inherited(){prepare();return {pending};}
 flChannel* adopt(const std::string&name,bool reversed,uint32_t flags)const{
  AHardwareBuffer*b[2]={buffers[reversed?1:0],buffers[reversed?0:1]};flChannel*ch=nullptr;
  CHECK(flCreateChannelFromHardwareBuffers(name.c_str(),64,32,FL_FORMAT_RGBA8,2,flags,b,&ch)==FL_OK);return ch;
 }
 int indexOf(flImage*image)const{
  const uint64_t id=flsample::ahbAllocationId(flImageAHardwareBuffer(image));CHECK(id);
  for(int i=0;i<2;++i)if(flsample::ahbAllocationId(buffers[i])==id)return i;
  return -1;
 }
#else
 gbm_device*gbm=nullptr;int node=-1;gbm_bo*bo[2]{};flDmabuf images[2]{};
 SharedImages(){
  // The first render node with a working GBM device, as FrameLink chooses.
  for(int i=128;i<136&&!gbm;++i){
   const std::string path="/dev/dri/renderD"+std::to_string(i);node=open(path.c_str(),O_RDWR|O_CLOEXEC);
   if(node<0)continue;gbm=gbm_create_device(node);if(!gbm){close(node);node=-1;}
  }
  CHECK(gbm);
  for(unsigned i=0;i<2;++i){
   // RENDERING|LINEAR is refused by some GBM backends; LINEAR alone suffices.
   bo[i]=gbm_bo_create(gbm,64,32,GBM_FORMAT_ABGR8888,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
   if(!bo[i])bo[i]=gbm_bo_create(gbm,64,32,GBM_FORMAT_ABGR8888,GBM_BO_USE_LINEAR);
   CHECK(bo[i]&&gbm_bo_get_modifier(bo[i])==DRM_FORMAT_MOD_LINEAR&&gbm_bo_get_plane_count(bo[i])==1);
   auto&d=images[i];for(auto&f:d.fd)f=-1;d.fd[0]=gbm_bo_get_fd(bo[i]);CHECK(d.fd[0]>=0);
   CHECK(fcntl(d.fd[0],F_SETFD,FD_CLOEXEC)==0);
   d.offset[0]=gbm_bo_get_offset(bo[i],0);d.stride[0]=gbm_bo_get_stride_for_plane(bo[i],0);
   d.planes=1;d.fourcc=DRM_FORMAT_ABGR8888;d.modifier=DRM_FORMAT_MOD_LINEAR;
  }
 }
 explicit SharedImages(const char*arg){
  for(unsigned i=0;i<2;++i){
   auto&d=images[i];for(auto&f:d.fd)f=-1;unsigned long long offset=0,stride=0;int consumed=0;
   CHECK(sscanf(arg,"%d:%llu:%llu%n",&d.fd[0],&offset,&stride,&consumed)==3&&d.fd[0]>=0);
   CHECK(fcntl(d.fd[0],F_SETFD,FD_CLOEXEC)==0);
   d.offset[0]=offset;d.stride[0]=stride;d.planes=1;d.fourcc=DRM_FORMAT_ABGR8888;d.modifier=DRM_FORMAT_MOD_LINEAR;
   arg+=consumed;if(i==0){CHECK(*arg==',');++arg;}
  }
 }
 ~SharedImages(){
  for(auto&d:images)if(d.fd[0]>=0)close(d.fd[0]);
  for(auto*b:bo)if(b)gbm_bo_destroy(b);if(gbm)gbm_device_destroy(gbm);if(node>=0)close(node);
 }
 std::string arg()const{
  std::string out;for(unsigned i=0;i<2;++i){if(i)out+=",";
   out+=std::to_string(images[i].fd[0])+":"+std::to_string(images[i].offset[0])+":"+std::to_string(images[i].stride[0]);}
  return out;
 }
 std::vector<NativeHandle> inherited()const{return {images[0].fd[0],images[1].fd[0]};}
 flChannel* adopt(const std::string&name,bool reversed,uint32_t flags)const{
  flDmabuf d[2]={images[reversed?1:0],images[reversed?0:1]};flChannel*ch=nullptr;
  CHECK(flCreateChannelFromDmabufs(name.c_str(),64,32,FL_FORMAT_RGBA8,2,flags,d,&ch)==FL_OK);return ch;
 }
 int indexOf(flImage*image)const{
  flDmabuf d{};CHECK(flImageDmabuf(image,&d)==FL_OK&&d.fd[0]>=0);struct stat a{};CHECK(fstat(d.fd[0],&a)==0);
  for(int i=0;i<2;++i){struct stat b{};CHECK(fstat(images[i].fd[0],&b)==0);if(a.st_dev==b.st_dev&&a.st_ino==b.st_ino)return i;}
  return -1;
 }
#endif
 SharedImages(const SharedImages&)=delete;SharedImages&operator=(const SharedImages&)=delete;
};

// The native producer's GstMemory reservations (the ones framelinkavsink's
// pool hands out), used directly by a source process.
#ifdef _WIN32
using ReservationOwner=FrameLinkD3DProducer;
using Reservation=FrameLinkD3DReservation;
#elif defined(__ANDROID__)
using ReservationOwner=FrameLinkAhbProducer;
using Reservation=FrameLinkAhbReservation;
#else
using ReservationOwner=FrameLinkDmabufProducer;
using Reservation=FrameLinkDmabufReservation;
#endif
inline Reservation* reservationOf(GstBuffer*b){
 auto*memory=gst_buffer_peek_memory(b,0);
 auto*r=static_cast<Reservation*>(gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),ReservationOwner::key()));CHECK(r);
#ifdef _WIN32
 CHECK(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(memory))==(ID3D11Resource*)flImageD3D11(r->buffer.image));
#elif defined(__ANDROID__)
 CHECK(flsample::ahbAllocationId(gst_framelink_memory_get_hardware_buffer(memory))==flsample::ahbAllocationId(flImageAHardwareBuffer(r->buffer.image)));
#else
 flDmabuf d{};CHECK(flImageDmabuf(r->buffer.image,&d)==FL_OK);struct stat a{},b2{};
 CHECK(fstat(gst_dmabuf_memory_get_fd(memory),&a)==0&&fstat(d.fd[0],&b2)==0&&a.st_ino==b2.st_ino&&a.st_dev==b2.st_dev);
#endif
 return r;
}
// Explicit source upload into a reservation, completed for submission.
inline void uploadReservation(ReservationOwner&owner,GstBuffer*b,unsigned char value){
 auto*image=reservationOf(b)->buffer.image;
#ifdef _WIN32
 std::vector<unsigned char> pixels(64*32*4,value);ID3D11DeviceContext*context=nullptr;
 flChannelD3D11Device(owner.channel)->GetImmediateContext(&context);gst_d3d11_device_lock(owner.device);
 context->UpdateSubresource(flImageD3D11(image),0,nullptr,pixels.data(),256,0);
 gst_d3d11_device_unlock(owner.device);context->Release();
#elif defined(__ANDROID__)
 auto*ahb=flImageAHardwareBuffer(image);CHECK(ahb);AHardwareBuffer_Desc d{};AHardwareBuffer_describe(ahb,&d);void*data=nullptr;
 CHECK(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data)==0&&data);
 for(unsigned y=0;y<32;++y)memset(static_cast<char*>(data)+size_t(y)*d.stride*4,value,256);
 CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0);(void)b;
 const int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);
 uint64_t one=1;CHECK(write(ready,&one,sizeof one)==sizeof one); // CPU writes are complete
 CHECK(owner.setReady(b,ready));close(ready);
#else
 (void)image;auto*mem=gst_buffer_peek_memory(b,0);const int fd=gst_dmabuf_memory_get_fd(mem);
 auto*layout=gst_buffer_get_video_meta(b);CHECK(layout);
 dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 void*data=mmap(nullptr,mem->size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(data!=MAP_FAILED);
 for(unsigned y=0;y<32;++y)memset(static_cast<char*>(data)+layout->offset[0]+y*layout->stride[0],value,256);
 CHECK(munmap(data,mem->size)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 const int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);
 uint64_t one=1;CHECK(write(ready,&one,sizeof one)==sizeof one); // CPU writes are complete
 CHECK(owner.setReady(b,ready));close(ready); // the owner keeps its own duplicate
#endif
}
#endif
}
