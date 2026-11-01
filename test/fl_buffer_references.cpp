#include "framelink.h"
#include <chrono>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 auto name="buffer-refs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 flChannel*c=nullptr,*p=nullptr;CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 flBuffer a{},b{},next{};CHECK(flAcquireBuffer(p,&a,1000)==FL_OK);CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
 uint64_t refs[256]{};for(auto& ref:refs)CHECK(flRetainBufferReference(p,&a,&ref)==FL_OK);
 uint64_t sentinel=17;CHECK(flRetainBufferReference(p,&a,&sentinel)==FL_BUSY&&sentinel==17);
 CHECK(flReleaseBufferReference(c,refs[0])==FL_INVALID);
 CHECK(flSubmit(p,&a,123)==FL_OK);CHECK(flRetainBufferReference(p,&a,&sentinel)==FL_INVALID&&sentinel==17);
 flFrame f{};CHECK(flAcquireFrame(c,&f,1000)==FL_OK);CHECK(flRelease(c,&f)==FL_OK);
 CHECK(flAcquireBuffer(p,&next,20)==FL_TIMEOUT); // remote done; local refs still prevent overwrite
 for(unsigned i=0;i<255;++i)CHECK(flReleaseBufferReference(p,refs[i])==FL_OK);
 CHECK(flReleaseBufferReference(p,refs[0])==FL_INVALID);
 CHECK(flAcquireBuffer(p,&next,20)==FL_TIMEOUT);
 // Leave another remote frame unfinished: waiting only on its consumed fence
 // must not hide completion of a local reference to the already-consumed slot.
 CHECK(flSubmit(p,&b,124)==FL_OK);CHECK(flAcquireFrame(c,&f,1000)==FL_OK);
 const auto started=std::chrono::steady_clock::now();
 // Release on a GstMemory-like finalizer thread while producer waits.
 std::thread finalizer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));CHECK(flReleaseBufferReference(p,refs[255])==FL_OK);});
 CHECK(flAcquireBuffer(p,&next,1000)==FL_OK&&next.slot==a.slot);finalizer.join();
 CHECK(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(500));
 CHECK(flRelease(c,&f)==FL_OK);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK&&b.slot!=next.slot);
 uint64_t pin=0;CHECK(flRetainBufferReference(p,&next,&pin)==FL_OK);CHECK(flCancelBuffer(p,&next)==FL_OK);
 CHECK(flAcquireBuffer(p,&next,10)==FL_TIMEOUT); // cancellation preserves live local holds
 CHECK(flCancelBuffer(p,&b)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_BUSY);
 CHECK(flAcquireBuffer(p,&next,1000)==FL_OK&&next.slot==b.slot);CHECK(flCancelBuffer(p,&next)==FL_OK);
 CHECK(flReleaseBufferReference(p,pin)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 CHECK(flReleaseBufferReference(p,pin)==FL_INVALID); // stale token after new ring
 flClose(p);flClose(c);
 puts("producer references: bounded 256, remote completion cannot reuse local-held slots, concurrent finalizer, cancel and resize guards pass");
}
