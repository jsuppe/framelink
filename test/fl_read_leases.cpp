#include "framelink.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int main() {
 std::string name="leases-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 flChannel *c=nullptr,*p=nullptr;
 auto r=flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c);
 if(r!=FL_OK)return 77;
 for(int i=0;i<100&&!p;++i){r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_NOT_FOUND);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 CHECK(p);
 flBuffer b{},extra{};flFrame f{};uint64_t first=0,second=0,reader=0;
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,101)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&first)==FL_OK);
 CHECK(flRetainReadLease(c,first,&reader)==FL_OK);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,202)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&second)==FL_OK);
 CHECK(flRelease(c,&f)==FL_BUSY);
 CHECK(flCompleteReadLease(c,second)==FL_OK && flCollectReadLeases(c)==FL_BUSY);
 CHECK(flAcquireBuffer(p,&extra,0)==FL_TIMEOUT);
 CHECK(flCompleteReadLease(c,first)==FL_OK && flCollectReadLeases(c)==FL_BUSY);
 CHECK(flAcquireBuffer(p,&extra,0)==FL_TIMEOUT);
 uint64_t generation=0;CHECK(flReadLeaseFrame(c,reader,&f,&generation)==FL_OK && f.ptsNs==101);
 CHECK(flCompleteReadLease(c,reader)==FL_OK && flCompleteReadLease(c,reader)==FL_INVALID);
 CHECK(flCollectReadLeases(c)==FL_OK);
 CHECK(flAcquireBuffer(p,&extra,1000)==FL_OK && flCancelBuffer(p,&extra)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,303)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&first)==FL_OK);
 CHECK(flCompleteReadLease(c,reader)==FL_INVALID);
 CHECK(flCompleteReadLease(c,first)==FL_OK && flCollectReadLeases(c)==FL_OK);
 flClose(p);flClose(c);
 std::puts("native read leases: retained readers block reuse; ordered retirement and resize pass");
}
