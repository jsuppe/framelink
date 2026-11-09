#include "framelink_session_transport.h"
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
int main(){
 // GIO asynchronously populates the Windows application registry cache at DLL
 // startup. Join that initialization explicitly before measuring CPU ownership.
 // This test needs no GStreamer plugin discovery.
 GList* apps=g_app_info_get_all();g_list_free_full(apps,g_object_unref);
 auto*warm=fl_gst_session_new(1);auto warmConfig=config();configure(warm,warmConfig);flSessionConfig warmOut{};CHECK(fl_gst_session_wait(warm,1,1,&warmOut)==FL_GST_SESSION_TIMEOUT);g_object_unref(warm);DWORD baseline=handles();
 flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
 // One warm cycle, followed by 100 cycles with exact handle equality.
 for(unsigned mode=0;mode<101;mode++){
  auto*s=fl_gst_session_new(1);auto*c=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);configure(c,cfg);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=4096;}
  Pair pair;HANDLE server=pair.take(false),client=pair.take(true);FlGstSessionResult sr=FL_GST_SESSION_INVALID;
  std::thread producer([&]{sr=fl_gst_session_bind_shared_cpu_pipe(s,TRUE,1,client,GetCurrentProcess(),1000,&req,0,nullptr,0);});
  CHECK(fl_gst_session_bind_shared_cpu_pipe(c,FALSE,1,server,GetCurrentProcess(),1000,&req,0,d,2)==FL_GST_SESSION_READY);producer.join();CHECK(sr==FL_GST_SESSION_READY);
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
  g_object_unref(s);g_object_unref(c);auto actualHandles=handles();if(!mode){fprintf(stderr,"warm baseline before=%lu after=%lu\n",baseline,actualHandles);baseline=actualHandles;}if(actualHandles!=baseline)fprintf(stderr,"mode=%u baseline=%lu actual=%lu\n",mode,baseline,actualHandles);CHECK(actualHandles==baseline);
 }
 // Cancel/reconfigure while setup is blocked: observe its outgoing hello first,
 // then interrupt. No completed old-generation pool can be claimed/published.
 for(bool replace:{false,true}){
  auto*s=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);
  Pair pair;HANDLE input=pair.take(true);FlGstSessionResult result=FL_GST_SESSION_READY;
  std::thread worker([&]{result=fl_gst_session_bind_shared_cpu_pipe(s,TRUE,1,input,GetCurrentProcess(),60000,&req,0,nullptr,0);});
  DWORD available=0;const auto deadline=g_get_monotonic_time()+2000000;
  do {CHECK(PeekNamedPipe(pair.server,nullptr,0,nullptr,&available,nullptr));if(available)break;Sleep(1);}while(g_get_monotonic_time()<deadline);
  CHECK(available>0);auto start=g_get_monotonic_time();
  if(replace){cfg.generation=2;configure(s,cfg);}else fl_gst_session_cancel(s);
  worker.join();CHECK(g_get_monotonic_time()-start<1000000);CHECK(result==(replace?FL_GST_SESSION_STALE:FL_GST_SESSION_CANCELLED));
  CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1));g_object_unref(s);CloseHandle(pair.server);pair.server=nullptr;CHECK(handles()==baseline);
 }
 puts("CPU coordinator: exact generation/role, one claim, metadata/EOS, held mappings through cancel/replacement, blocked setup interruption and 100-session handle cleanup passed");
}
