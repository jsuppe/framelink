#include "framelink.h"
#include "../src/fl_metadata.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifndef _WIN32
#include "framelink_sync_fd.h"
#include <sys/eventfd.h>
#include <unistd.h>
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::string name(){return "metadata-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());}
static void roundtrip(bool required){
 auto n=name();flChannel*c=nullptr,*p=nullptr;CHECK(flCreateChannelEx(n.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|(required?FL_REQUIRE_FRAME_METADATA:0),&c)==FL_OK);
 CHECK(flOpenProducer(n.c_str(),&p)==FL_OK);
 for(unsigned i=0;i<3;++i){
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
  flFrameMetadata m{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|(i==0?FL_FRAME_DISCONT:0),int64_t(100+i),1234567u+i*100,9u+i,{}};
  auto bad=m;bad.version=99;CHECK(flSetFrameMetadata(p,&b,&bad)==FL_VERSION_MISMATCH);
  bad=m;bad.reserved[1]=1;CHECK(flSetFrameMetadata(p,&b,&bad)==FL_INVALID);
  if(required)CHECK(flSubmit(p,&b,900+i)==FL_FORMAT_UNSUPPORTED); // does not consume reservation
  bool present=required||i!=1;
  if(present)CHECK(flSetFrameMetadata(p,&b,&m)==FL_OK);
#ifndef _WIN32
  int ready=-1;
  if(i==2){ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0);CHECK(flSubmitSyncFd(p,&b,ready,900+i)==FL_OK);}
  else
#endif
  CHECK(flSubmit(p,&b,900+i)==FL_OK);
  flFrame f{};
#ifndef _WIN32
  if(ready>=0){CHECK(flAcquireFrame(c,&f,10)==FL_TIMEOUT);uint64_t one=1;CHECK(write(ready,&one,sizeof(one))==sizeof(one));close(ready);}
#endif
  CHECK(flAcquireFrame(c,&f,1000)==FL_OK&&f.ptsNs==900+i);
  flFrameMetadata got{};memset(&got,0xa5,sizeof(got));auto sentinel=got;
  if(present)CHECK(flGetFrameMetadata(c,&f,&got)==FL_OK&&!memcmp(&got,&m,sizeof(m)));
  else CHECK(flGetFrameMetadata(c,&f,&got)==FL_NOT_FOUND&&!memcmp(&got,&sentinel,sizeof(got)));
  CHECK(flRelease(c,&f)==FL_OK);got=sentinel;CHECK(flGetFrameMetadata(c,&f,&got)==FL_INVALID&&!memcmp(&got,&sentinel,sizeof(got)));
 }
 flClose(p);flClose(c);
}
static void rejectLegacy(uint32_t features, bool wrongType=false, bool oversized=false){
 auto n=name();flChannel*c=nullptr;CHECK(flCreateChannelEx(n.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&c)==FL_OK);
 {auto peer=fl::Channel::connect(n,1000);CHECK(peer.connected());auto hello=peer.recv();CHECK(hello&&hello->type==fl::MsgType::Hello);
 fl::HelloMsg old{};old.version=FL_VERSION;old._pad=features;if(oversized){char bytes[sizeof(old)+1]{};memcpy(bytes,&old,sizeof(old));CHECK(peer.send(fl::MsgType::Hello,bytes,sizeof(bytes)));}
 else CHECK(peer.send(wrongType?fl::MsgType::FrameReady:fl::MsgType::Hello,old));auto refused=peer.recv();CHECK(!refused);}
 flClose(c);
}
int main(){roundtrip(false);roundtrip(true);rejectLegacy(0);rejectLegacy(1u|(2u<<16));rejectLegacy(1u,true);rejectLegacy(1u,false,true);puts("frame metadata: exact values, optional absence/reuse, required publication, readiness FD and legacy/unknown-required peer refusal pass");}
