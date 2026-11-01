#include "framelink.h"
#include "fl_ipc.h"
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 static_assert(FL_MAX_POOL_SIZE==16 && FL_VERSION==6,"expanded protocol contract");
 const std::string name="pool16-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 for(auto format:{FL_FORMAT_BGRA8,FL_FORMAT_NV12}){
  flChannel *consumer=nullptr,*producer=nullptr;
  CHECK(flCreateChannelEx(name.c_str(),64,64,format,FL_MAX_POOL_SIZE+1,FL_MAP_CPU,&consumer)==FL_INVALID && !consumer);
  CHECK(flCreateChannelEx(name.c_str(),64,64,format,FL_MAX_POOL_SIZE,FL_MAP_CPU,&consumer)==FL_OK);
  // Old-version peer must be refused after Hello, before receiving RingDesc,
  // handles/FDs. Backend source orders duplication strictly after this check.
  {
   auto peer=fl::Channel::connect(name,1000);CHECK(peer.connected());
   auto hello=peer.recv();CHECK(hello && hello->type==fl::MsgType::Hello && hello->fds.empty());
   auto* data=hello->as<fl::HelloMsg>();CHECK(data && data->version==FL_VERSION);
   fl::HelloMsg old{};old.version=FL_VERSION-1;CHECK(peer.send(fl::MsgType::Hello,old));
   CHECK(!peer.recv());
  }
  for(int i=0;i<100&&!producer;++i){auto r=flOpenProducer(name.c_str(),&producer);CHECK(r==FL_OK||r==FL_BUSY||r==FL_DISCONNECTED||r==FL_NOT_FOUND);if(!producer)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  CHECK(producer);flChannelInfo info{};CHECK(flQuery(producer,&info)==FL_OK && info.poolSize==16 && info.version==6);
  std::vector<flBuffer> held(16);std::vector<bool> seen(16);
  for(unsigned i=0;i<16;++i){flBuffer buffer{};CHECK(flAcquireBuffer(producer,&buffer,100)==FL_OK);CHECK(buffer.slot<16 && !seen[buffer.slot]);seen[buffer.slot]=true;held[buffer.slot]=buffer;}
  flBuffer blocked{};CHECK(flAcquireBuffer(producer,&blocked,10)==FL_TIMEOUT);
  // Hold every other slot reserved: success can only recycle each tested high
  // index, not repeatedly reuse slot0 and falsely qualify the expanded ring.
  for(unsigned slot:{15u,8u,0u}){
   auto& buffer=held[slot];CHECK(buffer.slot==slot);CHECK(flSubmit(producer,&buffer,1000+slot)==FL_OK);
   flFrame frame{};CHECK(flAcquireFrame(consumer,&frame,1000)==FL_OK);CHECK(frame.slot==slot && frame.ptsNs==1000+slot);
   CHECK(flRelease(consumer,&frame)==FL_OK);CHECK(flAcquireBuffer(producer,&buffer,1000)==FL_OK && buffer.slot==slot);
  }
  flClose(producer);flClose(consumer);
 }
 // New producer must reject an old consumer's Hello without sending its own
 // handshake or receiving any ring resources.
 const std::string oldName=name+"-old";
 std::thread oldConsumer([&]{auto peer=fl::Channel::listen(oldName);CHECK(peer.connected());fl::HelloMsg hello{};hello.version=FL_VERSION-1;CHECK(peer.send(fl::MsgType::Hello,hello));CHECK(!peer.recv());});
 flChannel* incompatible=nullptr;flResult result=FL_NOT_FOUND;
 for(int i=0;i<100 && result==FL_NOT_FOUND;++i){result=flOpenProducer(oldName.c_str(),&incompatible);if(result==FL_NOT_FOUND)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 CHECK(result==FL_VERSION_MISMATCH && !incompatible);oldConsumer.join();
 puts("16 distinct slots held; high slots recycled;17 rejected;v5 denied before ring transfer");
}
