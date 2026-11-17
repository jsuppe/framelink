#include "framelink_shared_cpu_win.h"
#include "../src/fl_shared_cpu_pipe_win.h"
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s error=%lu\n",__LINE__,#x,GetLastError());exit(1);}}while(0)
struct Pair {
 HANDLE server=nullptr,client=nullptr;
 Pair(bool asynchronous=true){
  static unsigned next=0;auto name=L"\\\\.\\pipe\\fl-cpu-pipe-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++next);
  server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
   PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);CHECK(server!=INVALID_HANDLE_VALUE);
  HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(event);OVERLAPPED o{};o.hEvent=event;
  CHECK(!ConnectNamedPipe(server,&o)&&GetLastError()==ERROR_IO_PENDING);
  client=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,asynchronous?FILE_FLAG_OVERLAPPED:0,nullptr);CHECK(client!=INVALID_HANDLE_VALUE);
  DWORD mode=PIPE_READMODE_MESSAGE;CHECK(SetNamedPipeHandleState(client,&mode,nullptr,nullptr));
  CHECK(WaitForSingleObject(event,1000)==WAIT_OBJECT_0);DWORD ignored=0;CHECK(GetOverlappedResult(server,&o,&ignored,FALSE));CloseHandle(event);
 }
 ~Pair(){if(server)CloseHandle(server);if(client)CloseHandle(client);}
 HANDLE take(bool producer){HANDLE h=producer?client:server;if(producer)client=nullptr;else server=nullptr;return h;}
};
static DWORD handles(){DWORD n=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&n));return n;}

static flSessionConfig session(bool nv12=false){
 flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=1;s.videoTrackId=1;
 s.video={uint32_t(nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8),16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;return s;
}
static flMemoryRequirements requirements(bool producer){
 flMemoryRequirements r{};CHECK(flMemoryRequirementsInit(&r)==FL_MEMORY_OK);
 r.allowedResourceTypes=FL_MEMORY_SHARED_CPU;r.allowedOperations=FL_MEMORY_COPY;
 r.requiredUsage=producer?FL_MEMORY_CPU_WRITE:FL_MEMORY_CPU_READ;return r;
}
struct Offer {
 flSharedCpuDescriptor d[2]{};fl::SharedCpuAllocationWin owner[2];HANDLE h[2]{};
 Offer(bool nv12=false){for(unsigned i=0;i<2;i++){
  auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.generation=1;v.allocationId=i+1;
  v.format=nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=nv12?2:1;
  v.planeStride[0]=nv12?16:64;v.allocationBytes=4096;
  if(nv12){v.planeStride[1]=16;v.planeOffset[1]=256;}
  CHECK(fl::SharedCpuAllocationWin::create(v,owner[i])==FL_OK);
  CHECK(owner[i].exportHandle(true,GetCurrentProcess(),h[i])==FL_OK);
 }}
 ~Offer(){for(auto v:h)if(v)CloseHandle(v);}
};
static fl::SharedCpuPipeWin adopt(HANDLE pipe,HANDLE peer=GetCurrentProcess()){
 fl::SharedCpuPipeWin out;CHECK(fl::SharedCpuPipeWin::adoptConnected(pipe,peer,nullptr,250,out)==FL_OK);return out;
}
static void pixels(fl::SharedCpuAllocationWin::Mapping& map,bool write,unsigned sequence=0){
 const auto& v=map.view();for(uint32_t p=0;p<v.planes;p++)for(uint32_t y=0;y<v.plane[p].rows;y++)
 for(uint32_t x=0;x<v.plane[p].rowBytes;x++){
  auto* b=static_cast<unsigned char*>(v.plane[p].pixels)+y*v.plane[p].stride+x;
  unsigned char expected=uint8_t(x+17*y+31*p+sequence);if(write)*b=expected;else CHECK(*b==expected);
 }
}
static void fill(flMappedImage& v,unsigned value,bool write){
 for(unsigned p=0;p<v.planes;p++)for(unsigned y=0;y<v.plane[p].rows;y++)for(unsigned x=0;x<v.plane[p].rowBytes;x++){
  auto*b=static_cast<unsigned char*>(v.plane[p].pixels)+y*v.plane[p].stride+x;if(write)*b=uint8_t(value);else CHECK(*b==uint8_t(value));
 }
}
static int child(const char* name,DWORD parent,bool nv12){
 HANDLE peer=OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parent);CHECK(peer);
 HANDLE pipe=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);CHECK(pipe!=INVALID_HANDLE_VALUE);
 DWORD mode=PIPE_READMODE_MESSAGE;CHECK(SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr));
 auto s=session(nv12);auto req=requirements(true);uint32_t flags=nv12?FL_SHARED_CPU_FRAME_METADATA:0;
 flSharedCpuChannel*c=nullptr;CHECK(flSharedCpuOpenProducerPipe(pipe,peer,nullptr,1000,&s,&req,FL_MEMORY_COPY,flags,&c)==FL_OK);CloseHandle(peer);
 flSessionConfig actual{};uint32_t ops=0,gotFlags=0,slots=0;int producer=0;
 CHECK(flSharedCpuQuery(c,&actual,&ops,&gotFlags,&producer,&slots)==FL_OK&&producer&&slots==2&&ops==FL_MEMORY_COPY&&gotFlags==flags);
 CHECK(flSharedCpuWaitReadable(c,0)==FL_TIMEOUT);
 flSharedCpuLease*held=nullptr;flMappedImage heldView{};CHECK(flSharedCpuAcquireWrite(c,1,&held,&heldView)==FL_OK);
 CHECK(flSharedCpuCancelWrite(held)==FL_OK);CHECK(flSharedCpuAcquireWrite(c,1,&held,&heldView)==FL_OK);fill(heldView,42,true);
 for(unsigned seq=1;seq<=8;seq++){
  flSharedCpuLease*l=nullptr;flMappedImage v{};CHECK(flSharedCpuAcquireWrite(c,0,&l,&v)==FL_OK);
  flSharedCpuLease*unchanged=nullptr;flMappedImage empty{};
  CHECK(flSharedCpuAcquireWrite(c,0,&unchanged,&empty)==FL_BUSY&&!unchanged&&!empty.planes);
  CHECK(flSharedCpuRetainRead(l)==FL_INVALID);fill(v,seq,true);
  if(nv12){flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,seq*1000-10,1000,77,{}};
   CHECK(flSharedCpuSubmitWithMetadata(l,seq,seq*1000,&meta)==FL_OK);
  }else CHECK(flSharedCpuSubmit(l,seq,seq*1000)==FL_OK);
  CHECK(flSharedCpuAcquireWrite(c,0,&unchanged,&empty)==FL_BUSY);
  CHECK(flSharedCpuWaitReadable(c,1000)==FL_OK);
  auto result=flSharedCpuReceiveRelease(c);CHECK(result==(seq==8?FL_DISCONNECTED:FL_OK));
 }
 flSharedCpuClose(c);fill(heldView,42,false);CHECK(flSharedCpuCancelWrite(held)==FL_DISCONNECTED);return 0;
}
static void crossProcess(bool nv12){
 static unsigned serial=0;std::string name="\\\\.\\pipe\\fl-cpu-control-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
 HANDLE server=CreateNamedPipeA(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
  PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);CHECK(server!=INVALID_HANDLE_VALUE);
 HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(event);OVERLAPPED o{};o.hEvent=event;
 CHECK(!ConnectNamedPipe(server,&o)&&GetLastError()==ERROR_IO_PENDING);
 char exe[MAX_PATH];CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));
 std::string cmd="\""+std::string(exe)+"\" --child "+name+" "+std::to_string(GetCurrentProcessId())+" "+(nv12?"1":"0");
 STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 CHECK(CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));CloseHandle(pi.hThread);
 CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);DWORD ignored=0;CHECK(GetOverlappedResult(server,&o,&ignored,FALSE));CloseHandle(event);
 auto s=session(nv12);auto req=requirements(false);Offer offer(nv12);flSharedCpuChannel*c=nullptr;
 CHECK(flSharedCpuOpenConsumerPipe(server,pi.hProcess,nullptr,1000,&s,&req,0,offer.d,2,nv12?FL_SHARED_CPU_FRAME_METADATA:0,&c)==FL_OK);
 for(unsigned seq=1;seq<=8;seq++){
  CHECK(flSharedCpuWaitReadable(c,1000)==FL_OK);
  flSharedCpuLease*l=nullptr;flMappedImage view{};uint64_t actual=0;int64_t pts=0;
  CHECK(flSharedCpuReceive(c,&l,&view,&actual,&pts)==FL_OK&&actual==seq&&pts==seq*1000);fill(view,seq,false);
  MEMORY_BASIC_INFORMATION memory{};CHECK(VirtualQuery(view.plane[0].pixels,&memory,sizeof(memory))==sizeof(memory));
  CHECK((memory.Protect&0xff)==PAGE_READONLY);
  flFrameMetadata meta{};CHECK(flSharedCpuReadMetadata(l,&meta)==FL_OK);
  if(nv12)CHECK(meta.version==1&&meta.dtsNs==seq*1000-10&&meta.durationNs==1000&&meta.streamGeneration==77);
  else CHECK(meta.version==0);
  CHECK(flSharedCpuCancelWrite(l)==FL_INVALID&&flSharedCpuSubmit(l,99,99)==FL_INVALID);
  CHECK(flSharedCpuRetainRead(l)==FL_OK);CHECK(flSharedCpuReleaseRead(l)==FL_OK);fill(view,seq,false);
  if(seq==8){flSharedCpuClose(c);c=nullptr;fill(view,seq,false);CHECK(flSharedCpuReadMetadata(l,&meta)==FL_OK);CHECK(flSharedCpuReleaseRead(l)==FL_DISCONNECTED);}
  else CHECK(flSharedCpuReleaseRead(l)==FL_OK);
 }
 CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);DWORD code=1;CHECK(GetExitCodeProcess(pi.hProcess,&code)&&code==0);CloseHandle(pi.hProcess);

}
static void publicEos(){
 Pair p;auto s=session();auto prod=requirements(true),cons=requirements(false);Offer offer;
 flSharedCpuChannel*c=nullptr;HANDLE server=p.take(false),client=p.take(true);
 std::thread writer([&]{flSharedCpuChannel*w=nullptr;
  CHECK(flSharedCpuOpenProducerPipe(client,GetCurrentProcess(),nullptr,1000,&s,&prod,0,FL_SHARED_CPU_ORDERED_EOS,&w)==FL_OK);
  flSharedCpuLease*l=nullptr;flMappedImage v{};CHECK(flSharedCpuAcquireWrite(w,0,&l,&v)==FL_OK);
  CHECK(flSharedCpuSendEos(w)==FL_BUSY);CHECK(flSharedCpuCancelWrite(l)==FL_OK);
  CHECK(flSharedCpuSendEos(w)==FL_OK);flSharedCpuClose(w);
 });
 CHECK(flSharedCpuOpenConsumerPipe(server,GetCurrentProcess(),nullptr,1000,&s,&cons,0,offer.d,2,FL_SHARED_CPU_ORDERED_EOS,&c)==FL_OK);
 flSharedCpuLease*l=nullptr;flMappedImage v{};uint64_t seq=77;int64_t pts=88;int eos=0;
 CHECK(flSharedCpuReceive(c,&l,&v,&seq,&pts)==FL_INVALID);
 CHECK(flSharedCpuReceiveEvent(c,&l,&v,&seq,&pts,&eos)==FL_OK&&eos==1&&!l&&seq==77&&pts==88);
 flSharedCpuClose(c);writer.join();
}
int main(int argc,char**argv){
 if(argc==5)return child(argv[2],DWORD(std::strtoul(argv[3],nullptr,10)),argv[4][0]=='1');
 crossProcess(false);crossProcess(true);const DWORD baseline=handles();
 for(int i=0;i<98;i++){crossProcess(i%2!=0);CHECK(handles()==baseline);}
 publicEos();CHECK(handles()==baseline);
 {Pair p;HANDLE input=p.take(true);flSharedCpuChannel*c=nullptr;
  CHECK(flSharedCpuOpenProducerPipe(input,GetCurrentProcess(),nullptr,0,nullptr,nullptr,0,0,&c)==FL_INVALID&&!c);
  DWORD flags=0;CHECK(!GetHandleInformation(input,&flags));}
 CHECK(handles()==baseline);
 puts("Windows public CPU API: 100 child sessions, 800 exact BGRA/NV12 frames, RO maps, metadata, retained read/write close lifetimes, reserved EOS and handle cleanup passed");
}
