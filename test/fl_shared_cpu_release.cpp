#include "../src/fl_shared_cpu_control.h"
#include <sys/socket.h>
#include <dirent.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int fdCount(){auto*d=opendir("/proc/self/fd");CHECK(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
struct Fixture {
 fl::Channel peer;
 std::unique_ptr<fl::SharedCpuControl> producer;
 fl::SharedCpuImportedPool pool;
 std::array<fl::SharedCpuAllocation,16> owners;
 std::array<int,16> handles;
 explicit Fixture(unsigned count=2){
  handles.fill(-1);int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair));
  peer=fl::Channel::adoptConnectedFd(pair[0],30);producer=std::make_unique<fl::SharedCpuControl>(fl::Channel::adoptConnectedFd(pair[1],30));
  flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=7;s.videoTrackId=1;s.video={FL_FORMAT_BGRA8,16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
  std::array<uint8_t,256> hello{};memcpy(hello.data(),"FCH1",4);hello[4]=1;hello[8]=2;
  CHECK(flSessionEncode(&s,hello.data()+32,160)==FL_SESSION_OK);CHECK(flMemoryRequirementsEncode(&req,hello.data()+192,64)==FL_MEMORY_OK);
  CHECK(peer.send(fl::MsgType::SharedCpuHello,hello.data(),hello.size()));CHECK(producer->negotiate(true,s,req,0)==FL_OK);
  std::array<uint8_t,16+16*128> wire{};memcpy(wire.data(),"FCP1",4);wire[4]=1;wire[8]=count;
  for(unsigned i=0;i<count;i++){
   flSharedCpuDescriptor d{};d.version=1;d.resourceType=FL_MEMORY_SHARED_CPU;d.format=FL_FORMAT_BGRA8;d.width=d.height=16;d.planeCount=1;d.generation=7;d.allocationId=i+1;d.allocationBytes=1024;d.planeStride[0]=64;
   CHECK(fl::SharedCpuAllocation::create(d,owners[i])==FL_OK);CHECK(owners[i].exportFd(true,handles[i])==FL_OK);
   CHECK(flSharedCpuDescriptorEncode(&d,wire.data()+16+128*i,128)==FL_MEMORY_OK);
  }
  CHECK(peer.sendWithFds(fl::MsgType::SharedCpuPool,wire.data(),16+count*128,handles.data(),count));
  CHECK(producer->receivePool(pool)==FL_OK);CHECK(producer->poolAccepted());
 }
 ~Fixture(){for(auto fd:handles)if(fd>=0)close(fd);}
 static std::array<uint8_t,40> release(unsigned slot,unsigned seq){
  std::array<uint8_t,40> w{};memcpy(w.data(),"FCR1",4);w[4]=1;w[8]=7;w[16]=seq;w[32]=slot;return w;
 }
 void sendRelease(unsigned slot,unsigned seq){auto w=release(slot,seq);CHECK(peer.send(fl::MsgType::SharedCpuReleased,w.data(),w.size()));}
};
int main(){
 const int baseline=fdCount();
 {Fixture f(16);auto&p=*f.producer;
  for(unsigned slot=0;slot<16;slot++)CHECK(p.submit({slot+1,1000,slot})==FL_OK);
  for(unsigned slot=0;slot<16;slot++)CHECK(p.submit({17,1001,slot})==FL_BUSY);
  // Independent slots release in reverse order, not a monotonic retirement
  // watermark that could unsafely free earlier held slots.
  for(unsigned slot=16;slot-->0;){f.sendRelease(slot,slot+1);fl::SharedCpuControl::Frame out;CHECK(p.receiveRelease(out)==FL_OK);CHECK(out.slot==slot&&out.sequence==slot+1);}
  for(unsigned slot=0;slot<16;slot++)CHECK(p.submit({17+slot,2000,slot})==FL_OK);
 }
 CHECK(fdCount()==baseline);
 for(unsigned fault=0;fault<15;fault++){
  {Fixture f;auto&p=*f.producer;CHECK(p.submit({1,100,0})==FL_OK);CHECK(p.submit({2,200,1})==FL_OK);
   fl::SharedCpuAllocation::Mapping held;CHECK(f.pool.map(0,held)==FL_OK);*static_cast<unsigned char*>(held.view().plane[0].pixels)=0x5a;
   auto w=Fixture::release(0,1);
   if(fault==0)w[0]='X';if(fault==1)w[4]=2;if(fault==2)w[8]=6;if(fault==3)w[16]=0;if(fault==4)w[16]=2;
   if(fault==5)w[24]=1;if(fault==6)w[32]=2;if(fault==7)w[36]=1;
   if(fault==11||fault==12){f.sendRelease(0,1);fl::SharedCpuControl::Frame first;CHECK(p.receiveRelease(first)==FL_OK);if(fault==12)CHECK(p.submit({3,300,0})==FL_OK);}
   if(fault==13)f.peer=fl::Channel{};
   else if(fault!=14)CHECK(f.peer.sendWithFds(fault==8?fl::MsgType::FrameReleased:fl::MsgType::SharedCpuReleased,w.data(),w.size()-(fault==9),fault==10?f.handles.data():nullptr,fault==10?1:0));
   fl::SharedCpuControl::Frame out{999,888,777};auto start=std::chrono::steady_clock::now();
   CHECK(p.receiveRelease(out)==(fault>=13?FL_DISCONNECTED:FL_INVALID));
   CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
   CHECK(out.sequence==999&&out.ptsNs==888&&out.slot==777&&!p.poolAccepted());
   CHECK(p.submit({4,400,0})==FL_INVALID); // failure is not permission to recycle
   f.pool=fl::SharedCpuImportedPool{};f.producer.reset();
   CHECK(*static_cast<unsigned char*>(held.view().plane[0].pixels)==0x5a);
  }
  CHECK(fdCount()==baseline);
 }
 puts("shared CPU releases: 16-slot saturation/reverse release, 15 hostile/replay/death/silent cases, retained mappings and FD baseline passed");
}
