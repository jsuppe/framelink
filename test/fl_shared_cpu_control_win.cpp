#include "../src/fl_shared_cpu_control_win.h"
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
static void put(unsigned char*p,uint32_t v){for(int i=0;i<4;i++)p[i]=uint8_t(v>>(8*i));}
static void put64(unsigned char*p,uint64_t v){put(p,uint32_t(v));put(p+4,uint32_t(v>>32));}
static void rawHello(fl::SharedCpuPipeWin& raw,bool producer,int fault=0){
 fl::Message m;CHECK(raw.receive(m)==FL_OK);CHECK(m.payload.size()==256);
 put(m.payload.data()+8,producer?1:2);put(m.payload.data()+12,producer?FL_MEMORY_COPY:0);
 auto r=requirements(producer);CHECK(flMemoryRequirementsEncode(&r,m.payload.data()+192,64)==FL_MEMORY_OK);
 if(fault==1)put(m.payload.data()+8,producer?2:1);
 if(fault==2)m.payload[32+32]^=1; // exact canonical session mismatch
 if(fault==3)put(m.payload.data()+16,2); // unsupported metadata feature
 if(fault==4)put(m.payload.data()+20,1); // reserved field
 if(fault==5)put(m.payload.data()+12,FL_MEMORY_READBACK);
 CHECK(raw.send(fl::MsgType::SharedCpuHello,m.payload.data(),m.payload.size())==FL_OK);
}
static void negative(int fault){
 Pair pair;auto raw=adopt(pair.take(false));fl::SharedCpuControlWin control(adopt(pair.take(true)));
 Offer offer;flResult result=FL_OK;fl::SharedCpuImportedPoolWin pool;
 std::thread worker([&]{result=control.negotiate(true,session(),requirements(true),FL_MEMORY_COPY);
  if(result==FL_OK)result=control.receivePool(pool);});
 rawHello(raw,false,fault<=5?fault:0);
 if(fault>5){
  std::array<unsigned char,288> wire{};put(wire.data(),0x31574346);put(wire.data()+4,1);put(wire.data()+8,2);
  for(int i=0;i<2;i++){CHECK(flSharedCpuDescriptorEncode(&offer.d[i],wire.data()+16+i*136,128)==FL_MEMORY_OK);
   put64(wire.data()+16+i*136+128,uint64_t(reinterpret_cast<uintptr_t>(offer.h[i])));}
  size_t size=wire.size();
  if(fault==6)put(wire.data(),0x31504346); // POSIX packet must not be accepted
  if(fault==7)put(wire.data()+12,1);
  if(fault==8)--size;
  if(fault==9)put64(wire.data()+16+136+128,0); // partial import cleanup
  if(fault==10)put64(wire.data()+16+136+128,~uint64_t(0)); // pseudo handle
  if(fault==11)put64(wire.data()+16+136+128,uint64_t(reinterpret_cast<uintptr_t>(offer.h[0]))); // alias
  HANDLE replacement=nullptr;
  if(fault==12){replacement=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(replacement);}
  if(fault==13)CHECK(offer.owner[1].exportHandle(false,GetCurrentProcess(),replacement)==FL_OK);
  if(replacement)put64(wire.data()+16+136+128,uint64_t(reinterpret_cast<uintptr_t>(replacement)));
  CHECK(raw.send(fl::MsgType::SharedCpuPool,wire.data(),size)==FL_OK);
  worker.join();if(replacement)CloseHandle(replacement);
 }else worker.join();
 CHECK(result!=FL_OK&&!control.poolAccepted()&&pool.size()==0);
}
static void ackNegative(int fault){
 Pair pair;auto raw=adopt(pair.take(true));fl::SharedCpuControlWin control(adopt(pair.take(false)));
 Offer offer;flResult result=FL_OK;
 std::thread worker([&]{result=control.negotiate(false,session(),requirements(false),0);
  if(result==FL_OK)result=control.sendPool(offer.d,offer.h,2);});
 rawHello(raw,true);fl::Message m;CHECK(raw.receive(m)==FL_OK&&m.type==fl::MsgType::SharedCpuPool);
 std::array<unsigned char,24> ack{};put(ack.data(),0x31414346);put(ack.data()+4,1);put64(ack.data()+8,1);put(ack.data()+16,2);
 if(fault==1)put(ack.data()+8,2);
 if(fault==2)put(ack.data()+16,3);
 if(fault==3)put(ack.data()+20,1);
 if(fault==4)raw.close();
 else if(fault!=5)CHECK(raw.send(fl::MsgType::SharedCpuAccepted,ack.data(),fault==6?23:24)==FL_OK);
 worker.join();CHECK(result!=FL_OK&&!control.poolAccepted());
}
static void pixels(fl::SharedCpuAllocationWin::Mapping& map,bool write,unsigned sequence=0){
 const auto& v=map.view();for(uint32_t p=0;p<v.planes;p++)for(uint32_t y=0;y<v.plane[p].rows;y++)
 for(uint32_t x=0;x<v.plane[p].rowBytes;x++){
  auto* b=static_cast<unsigned char*>(v.plane[p].pixels)+y*v.plane[p].stride+x;
  unsigned char expected=uint8_t(x+17*y+31*p+sequence);if(write)*b=expected;else CHECK(*b==expected);
 }
}
static int child(const char* name,DWORD parent,bool nv12){
 HANDLE peer=OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parent);CHECK(peer);
 HANDLE pipe=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);CHECK(pipe!=INVALID_HANDLE_VALUE);
 DWORD mode=PIPE_READMODE_MESSAGE;CHECK(SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr));
 fl::SharedCpuControlWin control(adopt(pipe,peer));CloseHandle(peer);
 CHECK(control.negotiate(true,session(nv12),requirements(true),FL_MEMORY_COPY,true,true)==FL_OK);
 fl::SharedCpuImportedPoolWin pool;CHECK(control.receivePool(pool)==FL_OK&&control.poolAccepted());
 for(unsigned seq=1;seq<=32;seq++){
  fl::SharedCpuControlWin::Frame frame{};frame.sequence=seq;frame.slot=seq%2;frame.ptsNs=1000+17*seq;
  frame.metadata={FL_FRAME_METADATA_VERSION,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|(seq==8?FL_FRAME_DISCONT:0),frame.ptsNs-3,17,9,{}};
  CHECK(control.writableSlot(frame.slot));
  fl::SharedCpuAllocationWin::Mapping m;CHECK(pool.map(frame.slot,m)==FL_OK);pixels(m,true,seq);
  CHECK(control.submit(frame)==FL_OK&&!control.writableSlot(frame.slot));
  auto next=frame;next.sequence++;CHECK(control.submit(next)==FL_BUSY); // unread lease cannot be overwritten
  if(seq==32){CHECK(control.sendEos()==FL_OK);CHECK(control.submit(next)==FL_INVALID);}
  fl::SharedCpuControlWin::Frame released;CHECK(control.receiveRelease(released)==FL_OK);
  CHECK(released.sequence==seq&&released.slot==frame.slot);
 }
 CHECK(!control.writableSlot(0)&&!control.writableSlot(1));
 return 0;
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
 fl::SharedCpuControlWin control(adopt(server,pi.hProcess));Offer offer(nv12);
 CHECK(control.negotiate(false,session(nv12),requirements(false),0,true,true)==FL_OK);
 CHECK(control.sendPool(offer.d,offer.h,2)==FL_OK&&control.poolAccepted());
 for(unsigned seq=1;seq<=32;seq++){
  fl::SharedCpuControlWin::Frame frame;bool eos=false;CHECK(control.receiveFrame(frame,&eos)==FL_OK&&!eos);
  CHECK(frame.sequence==seq&&frame.slot==seq%2&&frame.ptsNs==1000+17*seq);
  CHECK(frame.metadata.version==1&&frame.metadata.dtsNs==frame.ptsNs-3&&frame.metadata.durationNs==17&&frame.metadata.streamGeneration==9);
  CHECK(frame.metadata.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|(seq==8?FL_FRAME_DISCONT:0)));
  fl::SharedCpuAllocationWin::Mapping m;CHECK(offer.owner[frame.slot].map(false,m)==FL_OK);pixels(m,false,seq);
  if(seq==32){fl::SharedCpuControlWin::Frame end;CHECK(control.receiveFrame(end,&eos)==FL_OK&&eos);pixels(m,false,seq);}
  CHECK(control.release(frame.slot,frame.sequence)==FL_OK);
 }
 CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);DWORD code=1;CHECK(GetExitCodeProcess(pi.hProcess,&code)&&code==0);CloseHandle(pi.hProcess);

}
static void frameNegative(int fault){
 Pair pair;auto raw=adopt(pair.take(true));fl::SharedCpuControlWin control(adopt(pair.take(false)));
 Offer offer;flResult result=FL_OK;
 std::thread worker([&]{CHECK(control.negotiate(false,session(),requirements(false),0,true,true)==FL_OK);
  CHECK(control.sendPool(offer.d,offer.h,2)==FL_OK);
  fl::SharedCpuControlWin::Frame f;bool eos=false;
  if(fault==13||fault==14)CHECK(control.receiveFrame(f,&eos)==FL_OK&&!eos);
  f.sequence=777;result=control.receiveFrame(f,&eos);CHECK(f.sequence==777);
 });
 rawHello(raw,true);fl::Message m;CHECK(raw.receive(m)==FL_OK);
 std::array<unsigned char,24> ack{};put(ack.data(),0x31414346);put(ack.data()+4,1);put64(ack.data()+8,1);put(ack.data()+16,2);
 CHECK(raw.send(fl::MsgType::SharedCpuAccepted,ack.data(),ack.size())==FL_OK);
 std::array<unsigned char,80> wire{};put(wire.data(),0x314d4346);put(wire.data()+4,1);
 put64(wire.data()+8,1);put64(wire.data()+16,1);put64(wire.data()+24,100);
 put(wire.data()+40,1);put(wire.data()+44,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION);put64(wire.data()+48,90);put64(wire.data()+56,17);put64(wire.data()+64,1);
 if(fault==13||fault==14){CHECK(raw.send(fl::MsgType::SharedCpuReadyMetadata,wire.data(),80)==FL_OK);if(fault==14)put64(wire.data()+16,2);}
 if(fault==1)wire[0]^=1;
 if(fault==2)put(wire.data()+4,2);
 if(fault==3)put64(wire.data()+8,2);
 if(fault==4)put64(wire.data()+16,0);
 if(fault==5)put64(wire.data()+24,~uint64_t(0));
 if(fault==6)put(wire.data()+32,2);
 if(fault==7)put(wire.data()+36,1);
 if(fault==8)put(wire.data()+40,99);
 if(fault==9)put(wire.data()+44,0x80000000);
 if(fault==10)put64(wire.data()+48,~uint64_t(0));
 if(fault==11)put(wire.data()+72,1);
 if(fault>=15){wire.fill(0);put(wire.data(),0x31454346);put(wire.data()+4,1);put64(wire.data()+8,1);
  if(fault==15)put64(wire.data()+16,1); // no prior frame
  if(fault==16)put64(wire.data()+24,1);
  CHECK(raw.send(fl::MsgType::SharedCpuEos,wire.data(),32)==FL_OK);
 }else CHECK(raw.send(fl::MsgType::SharedCpuReadyMetadata,wire.data(),fault==12?79:80)==FL_OK);
 worker.join();CHECK(result!=FL_OK&&!control.poolAccepted());
}
static void releaseNegative(int fault){
 Pair pair;auto raw=adopt(pair.take(false));fl::SharedCpuControlWin control(adopt(pair.take(true)));
 Offer offer;flResult result=FL_OK;
 std::thread worker([&]{CHECK(control.negotiate(true,session(),requirements(true),FL_MEMORY_COPY)==FL_OK);
  fl::SharedCpuImportedPoolWin pool;CHECK(control.receivePool(pool)==FL_OK);
  CHECK(control.submit({1,100,0,{}})==FL_OK);fl::SharedCpuControlWin::Frame released;
  result=control.receiveRelease(released);
 });
 rawHello(raw,false);
 std::array<unsigned char,288> pool{};put(pool.data(),0x31574346);put(pool.data()+4,1);put(pool.data()+8,2);
 for(int i=0;i<2;i++){CHECK(flSharedCpuDescriptorEncode(&offer.d[i],pool.data()+16+i*136,128)==FL_MEMORY_OK);
  put64(pool.data()+16+i*136+128,uint64_t(reinterpret_cast<uintptr_t>(offer.h[i])));}
 CHECK(raw.send(fl::MsgType::SharedCpuPool,pool.data(),pool.size())==FL_OK);
 fl::Message m;CHECK(raw.receive(m)==FL_OK&&m.type==fl::MsgType::SharedCpuAccepted);
 CHECK(raw.receive(m)==FL_OK&&m.type==fl::MsgType::SharedCpuReady);
 std::array<unsigned char,40> wire{};put(wire.data(),0x31524346);put(wire.data()+4,1);put64(wire.data()+8,1);put64(wire.data()+16,1);
 if(fault==1)put64(wire.data()+8,2);
 if(fault==2)put64(wire.data()+16,2);
 if(fault==3)put64(wire.data()+24,100);
 if(fault==4)put(wire.data()+32,1); // not in use
 if(fault==5)put(wire.data()+36,1);
 if(fault==6)raw.close();else CHECK(raw.send(fl::MsgType::SharedCpuReleased,wire.data(),fault==7?39:40)==FL_OK);
 worker.join();CHECK(result!=FL_OK&&!control.poolAccepted());
}
static void emptyEos(){
 Pair pair;fl::SharedCpuControlWin producer(adopt(pair.take(true))),consumer(adopt(pair.take(false)));
 Offer offer;
 std::thread writer([&]{CHECK(producer.negotiate(true,session(),requirements(true),FL_MEMORY_COPY,true,true)==FL_OK);
  fl::SharedCpuImportedPoolWin pool;CHECK(producer.receivePool(pool)==FL_OK);
  CHECK(producer.sendEos()==FL_OK);CHECK(producer.sendEos()==FL_INVALID);
 });
 CHECK(consumer.negotiate(false,session(),requirements(false),0,true,true)==FL_OK);
 CHECK(consumer.sendPool(offer.d,offer.h,2)==FL_OK);
 fl::SharedCpuControlWin::Frame frame;frame.sequence=777;bool eos=false;
 CHECK(consumer.receiveFrame(frame,&eos)==FL_OK&&eos&&frame.sequence==777);
 eos=false;CHECK(consumer.receiveFrame(frame,&eos)==FL_OK&&eos);
 writer.join();
}
int main(int argc,char**argv){
 if(argc==5)return child(argv[2],DWORD(std::strtoul(argv[3],nullptr,10)),argv[4][0]=='1');
 crossProcess(false);crossProcess(true);const auto baseline=handles();
 for(int i=0;i<98;i++){crossProcess(i%2!=0);CHECK(handles()==baseline);}
 for(int repeat=0;repeat<3;repeat++){
  for(int i=1;i<=13;i++){negative(i);CHECK(handles()==baseline);}
  for(int i=1;i<=6;i++){ackNegative(i);CHECK(handles()==baseline);}
 }
 for(int i=1;i<=16;i++){frameNegative(i);CHECK(handles()==baseline);}
 for(int i=1;i<=7;i++){releaseNegative(i);CHECK(handles()==baseline);}
 emptyEos();CHECK(handles()==baseline);
 puts("Windows CPU control: 100 cross-process BGRA/NV12 streams of 32 exact frames/metadata/releases and held-slot EOS, 57 setup plus 23 frame/release/EOS negatives, exact handle cleanup passed");
}
