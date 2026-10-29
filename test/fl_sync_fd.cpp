#include "framelink.h"
#include "framelink_sync_fd.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
// eventfd is a deterministic pollable completion surrogate. Actual Vulkan
// sync-file export/import is a separate GPU integration qualification.
int main(){for(unsigned flags:{0u,(unsigned)FL_GPU_SYNC,(unsigned)FL_DEFER_RELEASE}){
 bool gpu=(flags&FL_GPU_SYNC)!=0;
 std::string name="sync-fd-"+std::to_string(getpid())+"-"+std::to_string(flags);
 flChannel *c=nullptr,*p=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,64,FL_FORMAT_RGBA8,1,FL_MAP_CPU|flags,&c)==FL_OK);
 for(int i=0;i<100&&!p;i++){flOpenProducer(name.c_str(),&p);std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(p);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,100)==FL_OK);
 int ready=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK),consumed=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(ready>=0&&consumed>=0);
 CHECK(flSubmitSyncFd(p,&b,ready,123)==FL_OK);
 flFrame frame{};
 if(!gpu)CHECK(flAcquireFrame(c,&frame,20)==FL_TIMEOUT);
 else {CHECK(flAcquireFrame(c,&frame,1000)==FL_OK);int borrowed=-1;CHECK(flFrameReadySyncFd(c,&frame,&borrowed)==FL_OK);CHECK(borrowed>=0);}
 uint64_t one=1;CHECK(write(ready,&one,sizeof(one))==sizeof(one));
 if(!gpu)CHECK(flAcquireFrame(c,&frame,1000)==FL_OK);
 CHECK(frame.ptsNs==123);
 if(flags&FL_DEFER_RELEASE) {
  CHECK(flRelease(c,&frame)==FL_OK);
  CHECK(flSignalConsumedSyncFd(c,frame._readyValue+1,consumed)==FL_INVALID);
  CHECK(flSignalConsumedSyncFd(c,frame._readyValue,consumed)==FL_OK);
 } else CHECK(flReleaseSyncFd(c,&frame,consumed)==FL_OK);
 CHECK(flAcquireBuffer(p,&b,20)==FL_TIMEOUT);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 flChannelInfo before{}; CHECK(flQuery(p,&before)==FL_OK); CHECK(before.width==64);
 CHECK(write(consumed,&one,sizeof(one))==sizeof(one));
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 flChannelInfo after{}; CHECK(flQuery(p,&after)==FL_OK); CHECK(after.width==128);
 close(ready);close(consumed);flClose(p);flClose(c);
 }puts("Ready/consumed FD transfer, CPU timeout and GPU-owned readiness passed");}
