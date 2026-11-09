#include "framelink_session_transport.h"
#include "../src/fl_shared_cpu_endpoint_win.h"
#define NOMINMAX
#include <windows.h>
#include <thread>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s error=%lu\n",__LINE__,#x,GetLastError());exit(1);}}while(0)
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

static flSessionConfig config(){flSessionConfig c{};c.version=2;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;c.videoTrackId=1;c.video={FL_FORMAT_BGRA8,16,16,60,1};c.videoQueueFrames=1;c.connectTimeoutMs=c.idleTimeoutMs=1000;return c;}
static void configure(FlGstSession*s,const flSessionConfig&c){flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]=c.video;CHECK(fl_gst_session_configure(s,&c,&caps,&caps)==FL_GST_SESSION_READY);}
static flMemoryRequirements cpuRequirements(){flMemoryRequirements r{};CHECK(flMemoryRequirementsInit(&r)==FL_MEMORY_OK);r.allowedResourceTypes=FL_MEMORY_SHARED_CPU;return r;}
static int child(const char*name,DWORD pid,GBytes*key){
 HANDLE peer=nullptr,pipe=nullptr;std::string narrow=name;std::wstring wide(narrow.begin(),narrow.end());
 CHECK(fl::SharedCpuEndpointWin::connect(wide,nullptr,3000,pipe,peer)==FL_OK);
 CHECK(GetProcessId(peer)==pid);
 auto*s=fl_gst_session_new(1);configure(s,config());auto req=cpuRequirements();
 CHECK(fl_gst_session_bind_authenticated_cpu_pipe(s,TRUE,1,pipe,peer,key,3000,&req,0,nullptr,0)==FL_GST_SESSION_READY);CloseHandle(peer);
 auto*c=fl_gst_session_claim_shared_cpu(s,TRUE,1);CHECK(c);
 for(unsigned seq=1;seq<=8;seq++){
  flSharedCpuLease*w=nullptr;flMappedImage v{};CHECK(flSharedCpuAcquireWrite(c,0,&w,&v)==FL_OK);memset(v.plane[0].pixels,seq,1024);
  flFrameMetadata m{};m.version=1;m.flags=FL_FRAME_HAS_DURATION;m.durationNs=100;m.streamGeneration=123;
  CHECK(flSharedCpuSubmitWithMetadata(w,seq,seq*250,&m)==FL_OK);CHECK(flSharedCpuReceiveRelease(c)==FL_OK);
 }
 CHECK(flSharedCpuSendEos(c)==FL_OK);
 // Keep the producing process alive until the consumer has drained and closed.
 CHECK(flSharedCpuReceiveRelease(c)==FL_DISCONNECTED);flSharedCpuClose(c);g_object_unref(s);return 0;
}
static void crossProcess(GBytes*key){
 static unsigned serial=0;std::string name="\\\\.\\pipe\\framelink-cpu-auth-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
 std::wstring wide(name.begin(),name.end());HANDLE server=nullptr;
 CHECK(fl::SharedCpuEndpointWin::listen(wide,server)==FL_OK);
 HANDLE duplicate=nullptr;CHECK(fl::SharedCpuEndpointWin::listen(wide,duplicate)==FL_ACCESS_DENIED&&!duplicate);
 char exe[MAX_PATH];CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));
 std::string cmd="\""+std::string(exe)+"\" --child "+name+" "+std::to_string(GetCurrentProcessId());
 STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};CHECK(CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));CloseHandle(pi.hThread);
 HANDLE pinned=nullptr;CHECK(fl::SharedCpuEndpointWin::accept(server,nullptr,5000,pinned)==FL_OK);
 CHECK(GetProcessId(pinned)==pi.dwProcessId);
 auto*s=fl_gst_session_new(1);configure(s,config());auto req=cpuRequirements();
 flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=4096;}
 CHECK(fl_gst_session_bind_authenticated_cpu_pipe(s,FALSE,1,server,pinned,key,3000,&req,0,d,2)==FL_GST_SESSION_READY);
 CloseHandle(pinned);
 auto*c=fl_gst_session_claim_shared_cpu(s,FALSE,1);CHECK(c);
 for(unsigned seq=1;seq<=8;seq++){
  flSharedCpuLease*r=nullptr;flMappedImage v{};uint64_t actual=0;int64_t pts=0;int eos=-1;
  CHECK(flSharedCpuReceiveEvent(c,&r,&v,&actual,&pts,&eos)==FL_OK&&!eos&&actual==seq&&pts==seq*250);
  flFrameMetadata m{};CHECK(flSharedCpuReadMetadata(r,&m)==FL_OK&&m.durationNs==100&&m.streamGeneration==123);
  for(unsigned i=0;i<1024;i++)CHECK(static_cast<unsigned char*>(v.plane[0].pixels)[i]==seq);
  CHECK(flSharedCpuReleaseRead(r)==FL_OK);
 }
 flSharedCpuLease*r=nullptr;flMappedImage v{};uint64_t seq=0;int64_t pts=0;int eos=0;
 CHECK(flSharedCpuReceiveEvent(c,&r,&v,&seq,&pts,&eos)==FL_OK&&eos&&!r);
 flSharedCpuClose(c);g_object_unref(s);CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);DWORD code=99;CHECK(GetExitCodeProcess(pi.hProcess,&code)&&code==0);CloseHandle(pi.hProcess);
}
int main(int argc,char**argv){
 guint8 keyBytes[32]{};keyBytes[0]=71;auto*key=g_bytes_new(keyBytes,32);
 keyBytes[0]=72;auto*wrong=g_bytes_new(keyBytes,32);
 if(argc==4&&!strcmp(argv[1],"--child"))return child(argv[2],DWORD(std::strtoul(argv[3],nullptr,10)),key);
 // GIO asynchronously populates the Windows application registry cache at DLL
 // startup. Join that initialization explicitly before measuring CPU ownership.
 // This test needs no GStreamer plugin discovery.
 GList* apps=g_app_info_get_all();g_list_free_full(apps,g_object_unref);
 for(unsigned i=0;i<3;i++)crossProcess(key);
 auto*warm=fl_gst_session_new(1);auto warmConfig=config();configure(warm,warmConfig);flSessionConfig warmOut{};CHECK(fl_gst_session_wait(warm,1,1,&warmOut)==FL_GST_SESSION_TIMEOUT);g_object_unref(warm);DWORD baseline=handles();
 {
  using Endpoint=fl::SharedCpuEndpointWin;
  std::wstring name=L"\\\\.\\pipe\\framelink-cpu-missing-"+std::to_wstring(GetCurrentProcessId());
  HANDLE server=nullptr,peer=nullptr,client=nullptr;
  CHECK(Endpoint::listen(L"\\\\remote\\pipe\\framelink-cpu-x",server)==FL_INVALID&&!server);
  auto start=GetTickCount64();
  CHECK(Endpoint::connect(name,nullptr,30,client,peer)==FL_TIMEOUT&&!client&&!peer);
  CHECK(GetTickCount64()-start<1000);
  CHECK(Endpoint::listen(name,server)==FL_OK);
  start=GetTickCount64();CHECK(Endpoint::accept(server,nullptr,30,peer)==FL_TIMEOUT&&!peer);
  CHECK(GetTickCount64()-start<1000);CloseHandle(server);
  HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(cancel);
  std::thread signal([&]{Sleep(20);SetEvent(cancel);});
  start=GetTickCount64();CHECK(Endpoint::connect(name,cancel,60000,client,peer)==FL_DISCONNECTED&&!client&&!peer);
  signal.join();CHECK(GetTickCount64()-start<1000);
  ResetEvent(cancel);CHECK(Endpoint::listen(name,server)==FL_OK);
  std::thread signalAccept([&]{Sleep(20);SetEvent(cancel);});
  start=GetTickCount64();CHECK(Endpoint::accept(server,cancel,60000,peer)==FL_DISCONNECTED&&!peer);
  signalAccept.join();CHECK(GetTickCount64()-start<1000);CloseHandle(server);CloseHandle(cancel);
  // Client-first rendezvous and already-connected accept, with exact handle
  // cleanup. Inspect the actual kernel DACL rather than trusting SDDL intent.
  for(unsigned cycle=0;cycle<10;cycle++){
   HANDLE clientPeer=nullptr;flResult result=FL_INTERNAL;
   std::thread first([&]{result=Endpoint::connect(name,nullptr,1000,client,clientPeer);});
   Sleep(10);CHECK(Endpoint::listen(name,server)==FL_OK);
   DWORD securityBytes=0;
   GetKernelObjectSecurity(server,DACL_SECURITY_INFORMATION,nullptr,0,&securityBytes);
   CHECK(securityBytes);std::vector<unsigned char> security(securityBytes);
   CHECK(GetKernelObjectSecurity(server,DACL_SECURITY_INFORMATION,security.data(),securityBytes,&securityBytes));
   PACL dacl=nullptr;BOOL present=FALSE,defaulted=FALSE;
   CHECK(GetSecurityDescriptorDacl(security.data(),&present,&dacl,&defaulted)&&present);
   CHECK(dacl&&dacl->AceCount==1);
   void* ace=nullptr;CHECK(GetAce(dacl,0,&ace));
   auto* allow=static_cast<ACCESS_ALLOWED_ACE*>(ace);CHECK(allow->Header.AceType==ACCESS_ALLOWED_ACE_TYPE);
   HANDLE token=nullptr;CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));
   DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<unsigned char> user(bytes);
   CHECK(GetTokenInformation(token,TokenUser,user.data(),bytes,&bytes));
   CHECK(EqualSid(&allow->SidStart,reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid));
   CloseHandle(token);
   first.join();CHECK(result==FL_OK);
   CHECK(Endpoint::accept(server,nullptr,1000,peer)==FL_OK);
   CHECK(GetProcessId(peer)==GetCurrentProcessId()&&GetProcessId(clientPeer)==GetCurrentProcessId());
   for(HANDLE h:{server,client,peer,clientPeer}){DWORD flags=0;CHECK(GetHandleInformation(h,&flags)&&!(flags&HANDLE_FLAG_INHERIT));CloseHandle(h);}
   CHECK(handles()==baseline);
  }
  // A byte-mode impostor must never be mistaken for a CPU endpoint.
  server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
   PIPE_TYPE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);
  CHECK(server!=INVALID_HANDLE_VALUE);
  CHECK(Endpoint::connect(name,nullptr,1000,client,peer)==FL_INVALID&&!client&&!peer);
  CloseHandle(server);CHECK(handles()==baseline);
 }
 flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
 // Warm all five lifecycle paths, then require exact handle equality for 100 cycles.
 // Cold cancellation may initialize a Windows WaitCompletionPacket; retain each
 // warm count in the log rather than attributing it to a component without proof.
 for(unsigned mode=0;mode<105;mode++){
  auto*s=fl_gst_session_new(1);auto*c=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);configure(c,cfg);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=4096;}
  Pair pair;HANDLE server=pair.take(false),client=pair.take(true);FlGstSessionResult sr=FL_GST_SESSION_INVALID;
  std::thread producer([&]{sr=fl_gst_session_bind_authenticated_cpu_pipe(s,TRUE,1,client,GetCurrentProcess(),key,1000,&req,0,nullptr,0);});
  CHECK(fl_gst_session_bind_authenticated_cpu_pipe(c,FALSE,1,server,GetCurrentProcess(),key,1000,&req,0,d,2)==FL_GST_SESSION_READY);producer.join();CHECK(sr==FL_GST_SESSION_READY);
  CHECK(!fl_gst_session_claim_shared_cpu(c,TRUE,1));CHECK(!fl_gst_session_claim_shared_cpu(c,FALSE,2));
  {Pair duplicate;HANDLE input=duplicate.take(false);CHECK(fl_gst_session_bind_shared_cpu_pipe(c,FALSE,1,input,GetCurrentProcess(),100,&req,0,d,2)==FL_GST_SESSION_INVALID);DWORD flags=0;CHECK(!GetHandleInformation(input,&flags));}
  flSessionConfig out{};CHECK(fl_gst_session_wait(c,1,1,&out)==FL_GST_SESSION_TIMEOUT); // binding is not track readiness
  auto*p=fl_gst_session_claim_shared_cpu(s,TRUE,1);CHECK(p);CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1));
  if(mode%5==4){auto*q=fl_gst_session_claim_shared_cpu(c,FALSE,1);CHECK(q);flSharedCpuClose(p);
   flSharedCpuLease*l=nullptr;flMappedImage v{};uint64_t seq=0;int64_t pts=0;int eos=7;
   CHECK(flSharedCpuReceiveEvent(q,&l,&v,&seq,&pts,&eos)==FL_DISCONNECTED&&eos==7);flSharedCpuClose(q);
  }else if(mode%5==3){fl_gst_session_cancel(c);CHECK(!fl_gst_session_claim_shared_cpu(c,FALSE,1));CHECK(flSharedCpuReceiveRelease(p)==FL_DISCONNECTED);flSharedCpuClose(p);}
  else{
   auto*q=fl_gst_session_claim_shared_cpu(c,FALSE,1);CHECK(q);
   flSharedCpuLease*w=nullptr,*r=nullptr;flMappedImage view{},read{};
   CHECK(flSharedCpuAcquireWrite(p,0,&w,&view)==FL_OK);memset(view.plane[0].pixels,73,1024);
   flFrameMetadata m{};m.version=1;m.streamGeneration=123;m.flags=FL_FRAME_HAS_DURATION;m.durationNs=100;
   CHECK(flSharedCpuSubmitWithMetadata(w,1,250,&m)==FL_OK);CHECK(flSharedCpuSendEos(p)==FL_OK);
   uint64_t seq=0;int64_t pts=0;int eos=-1;CHECK(flSharedCpuReceiveEvent(q,&r,&read,&seq,&pts,&eos)==FL_OK);CHECK(!eos&&seq==1&&pts==250);
   flFrameMetadata actual{};CHECK(flSharedCpuReadMetadata(r,&actual)==FL_OK&&actual.streamGeneration==123&&actual.durationNs==100);
   flSharedCpuLease*none=nullptr;CHECK(flSharedCpuReceiveEvent(q,&none,&view,&seq,&pts,&eos)==FL_OK&&eos&&!none);
   if(mode%5==1)fl_gst_session_cancel(c);
   if(mode%5==2){cfg.generation=2;configure(c,cfg);CHECK(!fl_gst_session_claim_shared_cpu(c,FALSE,1));}
   for(unsigned j=0;j<1024;j++)CHECK(static_cast<unsigned char*>(read.plane[0].pixels)[j]==73);
   CHECK(flSharedCpuReleaseRead(r)==(mode%5?FL_DISCONNECTED:FL_OK));CHECK(flSharedCpuReceiveRelease(p)==(mode%5?FL_DISCONNECTED:FL_OK));
   flSharedCpuClose(q);flSharedCpuClose(p);
  }
  g_object_unref(s);g_object_unref(c);auto actualHandles=handles();if(mode<5){fprintf(stderr,"warm mode=%u baseline before=%lu after=%lu\n",mode,baseline,actualHandles);baseline=actualHandles;}if(actualHandles!=baseline)fprintf(stderr,"mode=%u baseline=%lu actual=%lu\n",mode,baseline,actualHandles);CHECK(actualHandles==baseline);
 }
 // Cancel/reconfigure while setup is blocked: observe its outgoing hello first,
 // then interrupt. No completed old-generation pool can be claimed/published.
 for(bool replace:{false,true}){
  auto*s=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);
  Pair pair;HANDLE input=pair.take(true);FlGstSessionResult result=FL_GST_SESSION_READY;
  std::thread worker([&]{result=fl_gst_session_bind_authenticated_cpu_pipe(s,TRUE,1,input,GetCurrentProcess(),key,60000,&req,0,nullptr,0);});
  DWORD available=0;const auto deadline=g_get_monotonic_time()+2000000;
  do {CHECK(PeekNamedPipe(pair.server,nullptr,0,nullptr,&available,nullptr));if(available)break;Sleep(1);}while(g_get_monotonic_time()<deadline);
  CHECK(available>0);auto start=g_get_monotonic_time();
  if(replace){cfg.generation=2;configure(s,cfg);}else fl_gst_session_cancel(s);
  worker.join();CHECK(g_get_monotonic_time()-start<1000000);CHECK(result==(replace?FL_GST_SESSION_STALE:FL_GST_SESSION_CANCELLED));
  CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1));g_object_unref(s);CloseHandle(pair.server);pair.server=nullptr;CHECK(handles()==baseline);
 }
 // Different credentials/configuration/roles cannot authorize the same pipe.
 for(unsigned mode=0;mode<8;mode++){
  auto*s=fl_gst_session_new(1);auto*c=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);
  if(mode==1)cfg.generation++;if(mode==2)cfg.epochNs++;if(mode==3)cfg.clockId.bytes[0]++;
  if(mode==4)cfg.sessionId.bytes[0]++;
  configure(c,cfg);Pair pair;auto input=pair.take(true);auto output=pair.take(false);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=cfg.generation;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=4096;}
  FlGstSessionResult sr=FL_GST_SESSION_READY;
  auto start=g_get_monotonic_time();
  std::thread p([&]{sr=fl_gst_session_bind_authenticated_cpu_pipe(s,TRUE,1,input,GetCurrentProcess(),key,100,&req,0,nullptr,0);});
  FlGstSessionResult cr;
  if(mode==6)cr=fl_gst_session_bind_shared_cpu_pipe(c,FALSE,cfg.generation,output,GetCurrentProcess(),100,&req,0,d,2); // no downgrade
  else cr=fl_gst_session_bind_authenticated_cpu_pipe(c,mode==5,cfg.generation,output,GetCurrentProcess(),mode==0?wrong:mode==7?nullptr:key,100,&req,0,mode==5?nullptr:d,mode==5?0:2);
  p.join();CHECK(sr!=FL_GST_SESSION_READY&&cr!=FL_GST_SESSION_READY);
  CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1)&&!fl_gst_session_claim_shared_cpu(c,FALSE,cfg.generation));
  CHECK(g_get_monotonic_time()-start<1000000);g_object_unref(s);g_object_unref(c);CHECK(handles()==baseline);
 }
 // Silent authenticated peer: finite whole-authentication deadline and no pool.
 {auto*s=fl_gst_session_new(1);configure(s,config());Pair pair;auto start=g_get_monotonic_time();
  CHECK(fl_gst_session_bind_authenticated_cpu_pipe(s,TRUE,1,pair.take(true),GetCurrentProcess(),key,30,&req,0,nullptr,0)!=FL_GST_SESSION_READY);
  CHECK(g_get_monotonic_time()-start<1000000);CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1));g_object_unref(s);}
 CHECK(handles()==baseline);g_bytes_unref(key);g_bytes_unref(wrong);
 puts("Authenticated CPU coordinator: 8 mismatch/downgrade negatives, silent deadline, exact generation/role, one claim, metadata/EOS, held mappings through cancel/replacement, blocked setup interruption and 100-session handle cleanup passed");
}
