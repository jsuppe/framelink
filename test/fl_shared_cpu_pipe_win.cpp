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
static void exchange(){
 Pair pair;fl::SharedCpuPipeWin a,b;CHECK(fl::SharedCpuPipeWin::adoptConnected(pair.take(false),GetCurrentProcess(),nullptr,500,a)==FL_OK);
 CHECK(fl::SharedCpuPipeWin::adoptConnected(pair.take(true),GetCurrentProcess(),nullptr,500,b)==FL_OK);
 const char payload[]="exact CPU control bytes";CHECK(a.send(fl::MsgType::SharedCpuHello,payload,sizeof(payload))==FL_OK);
 fl::Message message;CHECK(b.receive(message)==FL_OK&&message.fds.empty()&&message.type==fl::MsgType::SharedCpuHello);
 CHECK(message.payload.size()==sizeof(payload)&&!memcmp(message.payload.data(),payload,sizeof(payload)));
 CHECK(b.send(fl::MsgType::SharedCpuAccepted,nullptr,0)==FL_OK);CHECK(a.receive(message)==FL_OK&&message.payload.empty());
}
int main(){
 exchange(); // warm OS/CRT before exact handle-return checks
 const DWORD baseline=handles();
 for(unsigned n=0;n<100;n++){exchange();CHECK(handles()==baseline);}
 {Pair p(false);fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),nullptr,50,pipe)==FL_INVALID);CHECK(!pipe.connected());}
 {Pair p;fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),nullptr,0,pipe)==FL_INVALID);}
 {Pair p;fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),nullptr,nullptr,50,pipe)==FL_INVALID);}
 {Pair p;fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),nullptr,30,pipe)==FL_OK);
  fl::Message m;m.payload={99};auto start=GetTickCount64();CHECK(pipe.receive(m)==FL_TIMEOUT);CHECK(GetTickCount64()-start<1000&&!pipe.connected()&&m.payload[0]==99);}
 {Pair p;HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(cancel);fl::SharedCpuPipeWin pipe;
  CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),cancel,5000,pipe)==FL_OK);
  std::thread trigger([&]{Sleep(20);CHECK(SetEvent(cancel));});fl::Message m;auto start=GetTickCount64();
  CHECK(pipe.receive(m)==FL_DISCONNECTED);trigger.join();CHECK(GetTickCount64()-start<1000&&!pipe.connected());CloseHandle(cancel);}
 {Pair p;fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),nullptr,30,pipe)==FL_OK);
  // Peer deliberately does not read. Oversized relative to pipe quota: must
  // cancel/drain pending WRITE as safely as a pending READ.
  std::array<char,60000> payload{};auto start=GetTickCount64();CHECK(pipe.send(fl::MsgType::SharedCpuPool,payload.data(),payload.size())==FL_TIMEOUT);
  CHECK(GetTickCount64()-start<1000&&!pipe.connected());}
 {Pair p;fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),nullptr,500,pipe)==FL_OK);
  CloseHandle(p.server);p.server=nullptr;fl::Message m;CHECK(pipe.receive(m)==FL_DISCONNECTED&&!pipe.connected());}
 {Pair p;fl::SharedCpuPipeWin pipe;CHECK(fl::SharedCpuPipeWin::adoptConnected(p.take(true),GetCurrentProcess(),nullptr,500,pipe)==FL_OK);
  std::array<uint32_t,4> bad{fl::kMagic,uint32_t(fl::MsgType::SharedCpuHello),0,123};OVERLAPPED o{};o.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(o.hEvent);
  DWORD written=0;BOOL ok=WriteFile(p.server,bad.data(),sizeof(bad),&written,&o);if(!ok){CHECK(GetLastError()==ERROR_IO_PENDING);CHECK(GetOverlappedResult(p.server,&o,&written,TRUE));}CloseHandle(o.hEvent);
  fl::Message m;m.payload={99};CHECK(pipe.receive(m)==FL_INVALID&&!pipe.connected()&&m.payload[0]==99);}
 CHECK(handles()==baseline);puts("Windows CPU pipe: exact frames, 100 handle-clean exchanges, async-only adoption, timeout/cancel/read/write/death/malformed negatives passed");
}
