#pragma once
// Platform adapter for the shared native relay tests (fl_gst_native_relay.cpp):
// D3D11 on Windows, DMA-BUF on desktop Linux. Everything platform-specific in
// those tests lives here: the native memory policy, writing a producer frame,
// one-way control gates between processes, and child processes. The test
// cases themselves are the same on every platform.
#include "../gst/framelink_session_transport.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include "framelink_d3d11.h"
#else
#include <cerrno>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "framelink_dmabuf.h"
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
 std::string arg()const{return std::to_string(fd[0])+","+std::to_string(fd[1]);}
 std::vector<int> inherited()const{return {fd[0],fd[1]};}
#endif
 Gate(const Gate&)=delete;Gate&operator=(const Gate&)=delete;
};

// A child process running this same test executable with the given arguments.
// It inherits only the listed gates (never textures, fds of rings, or keys) and
// dies with the parent. join() requires a clean exit within 10 s.
struct Child {
#ifdef _WIN32
 PROCESS_INFORMATION process{};
 Child(const std::vector<std::string>&args,const std::vector<const Gate*>&gates){
  static HANDLE job=nullptr;
  if(!job){job=CreateJobObjectA(nullptr,nullptr);CHECK(job);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
   limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
   CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits));}
  char exe[MAX_PATH]{};CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));
  std::string command="\""+std::string(exe)+"\"";for(auto&a:args)command+=" \""+a+"\"";
  std::vector<HANDLE> handles;for(auto*g:gates)for(auto h:g->inherited())handles.push_back(h);
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
 ~Child(){if(process.hProcess){TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);}}
#else
 pid_t pid=-1;
 Child(const std::vector<std::string>&args,const std::vector<const Gate*>&gates){
  std::vector<int> fds;for(auto*g:gates)for(int f:g->inherited())fds.push_back(f);
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
 ~Child(){if(pid>0){kill(pid,SIGKILL);waitpid(pid,nullptr,0);}}
#endif
 Child(const Child&)=delete;Child&operator=(const Child&)=delete;
};

}
