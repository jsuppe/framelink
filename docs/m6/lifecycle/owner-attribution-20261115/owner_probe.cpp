// Diagnostic only: persistent-producer retained-device memory attribution.
// probe parent <arm:none|open|submit|map> <cycles> <out.csv>
// probe child <name> <arm>
#include "framelink.h"
#include "framelink_d3d11.h"
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(2);}}while(0)
static const unsigned W=1920,H=1080,FRAMES=300;
struct Sample{unsigned long long priv,heapBusy,heapCommitted,vqPrivate;DWORD handles;};
static Sample sample(){Sample s{};PROCESS_MEMORY_COUNTERS_EX m{};K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m);s.priv=m.PrivateUsage/1024;
 GetProcessHandleCount(GetCurrentProcess(),&s.handles);
 std::vector<HANDLE> heaps(256);DWORD n=GetProcessHeaps(256,heaps.data());
 for(DWORD i=0;i<n&&i<256;i++){HeapLock(heaps[i]);PROCESS_HEAP_ENTRY e{};while(HeapWalk(heaps[i],&e)){if(e.wFlags&PROCESS_HEAP_ENTRY_BUSY)s.heapBusy+=e.cbData;else if(e.wFlags&PROCESS_HEAP_REGION)s.heapCommitted+=e.Region.dwCommittedSize;}HeapUnlock(heaps[i]);}
 s.heapBusy/=1024;s.heapCommitted/=1024;
 MEMORY_BASIC_INFORMATION mb{};for(char*p=nullptr;VirtualQuery(p,&mb,sizeof mb);p=(char*)mb.BaseAddress+mb.RegionSize)if(mb.State==MEM_COMMIT&&mb.Type==MEM_PRIVATE)s.vqPrivate+=mb.RegionSize/1024;
 return s;}
static int child(const char*name,const char*arm){
 flChannel*c=nullptr;CHECK(flCreateChannelEx(name,W,H,FL_FORMAT_BGRA8,2,FL_MAP_CPU,&c)==FL_OK);
 HANDLE ready=OpenEventA(EVENT_MODIFY_STATE,FALSE,(std::string(name)+"-ready").c_str());CHECK(ready);SetEvent(ready);CloseHandle(ready);
 unsigned got=0;for(;;){flFrame f{};auto r=flAcquireFrame(c,&f,10000);if(r==FL_OK){++got;flRelease(c,&f);continue;}if(r==FL_TIMEOUT&&got==0&&!strcmp(arm,"open"))continue;break;}
 flClose(c);return 0;}
int main(int argc,char**argv){
 if(argc>=4&&!strcmp(argv[1],"child"))return child(argv[2],argv[3]);
 CHECK(argc==5&&!strcmp(argv[1],"parent"));const std::string arm=argv[2];const int cycles=atoi(argv[3]);
 FILE*out=fopen(argv[4],"w");CHECK(out);fprintf(out,"cycle,frames,priv_kib,heap_busy_kib,heap_committed_kib,vq_private_kib,handles\n");
 flD3D11DeviceOwner*owner=nullptr;if(arm!="none")CHECK(flCreateD3D11DeviceOwner(nullptr,&owner)==FL_OK);
 char exe[MAX_PATH];GetModuleFileNameA(nullptr,exe,MAX_PATH);
 std::vector<unsigned char> pattern(W*4);
 auto row=[&](int i,unsigned frames){auto s=sample();fprintf(out,"%d,%u,%llu,%llu,%llu,%llu,%lu\n",i,frames,s.priv,s.heapBusy,s.heapCommitted,s.vqPrivate,s.handles);fflush(out);};
 row(-1,0);
 for(int i=0;i<cycles;i++){
  std::string name="flprobe-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(i);
  HANDLE ready=CreateEventA(nullptr,TRUE,FALSE,(name+"-ready").c_str());CHECK(ready);
  std::string cmd="\""+std::string(exe)+"\" child "+name+" "+(arm=="none"?"open":arm);
  STARTUPINFOA si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
  unsigned frames=0;
  if(arm=="none"){Sleep(5000);}else{
   CHECK(CreateProcessA(nullptr,&cmd[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));CloseHandle(pi.hThread);
   CHECK(WaitForSingleObject(ready,20000)==WAIT_OBJECT_0);
   flChannel*p=nullptr;flResult r;while((r=flOpenProducerD3D11BoundedWithDeviceOwner(owner,name.c_str(),5000,nullptr,&p))==FL_NOT_FOUND)Sleep(5);
   CHECK(r==FL_OK);
   LARGE_INTEGER f,t0,t;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
   if(arm=="open")Sleep(5000);
   else for(unsigned k=0;k<FRAMES;k++){
    flBuffer b{};{auto ar=flAcquireBuffer(p,&b,1000);if(ar!=FL_OK){fprintf(stderr,"acquire %d at %u: %s\n",(int)ar,k,flResultString(ar));exit(3);}}
    if(arm=="map"){void*d=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&d,&stride)==FL_OK);memset(pattern.data(),k&255,pattern.size());for(unsigned y=0;y<H;y++)memcpy((char*)d+y*stride,pattern.data(),pattern.size());flImageUnmap(b.image);}
    CHECK(flSubmit(p,&b,(int64_t)k*16666667)==FL_OK);++frames;
    for(;;){QueryPerformanceCounter(&t);if((t.QuadPart-t0.QuadPart)*60>=(long long)(k+1)*f.QuadPart)break;Sleep(1);}
   }
   flClose(p);CHECK(WaitForSingleObject(pi.hProcess,20000)==WAIT_OBJECT_0);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);CHECK(code==0);
  }
  CloseHandle(ready);row(i,frames);
 }
 for(int s=0;s<6;s++){Sleep(5000);row(1000+s,0);}
 if(owner)flReleaseD3D11DeviceOwner(owner);row(2000,0);fclose(out);return 0;}
