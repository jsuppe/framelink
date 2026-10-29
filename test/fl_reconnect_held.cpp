#include "framelink.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 flChannel *c=nullptr,*p=nullptr,*replacement=nullptr;
 CHECK(flCreateChannelEx("reconnect-held",64,64,FL_FORMAT_RGBA8,1,FL_MAP_CPU,&c)==FL_OK);
 for(int i=0;i<100&&!p;i++){flOpenProducer("reconnect-held",&p);std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(p);
 flChannelInfo before{};CHECK(flQuery(p,&before)==FL_OK);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,100)==FL_OK);
 CHECK(flSubmit(p,&b,111)==FL_OK);flFrame f{};CHECK(flAcquireFrame(c,&f,1000)==FL_OK);
 flClose(p);p=nullptr;std::this_thread::sleep_for(std::chrono::milliseconds(30));
 CHECK(flOpenProducer("reconnect-held",&replacement)==FL_BUSY);CHECK(!replacement);
 auto release=flRelease(c,&f);CHECK(release==FL_OK || release==FL_DISCONNECTED);
 for(int i=0;i<100&&!replacement;i++){auto r=flOpenProducer("reconnect-held",&replacement);CHECK(r==FL_OK||r==FL_BUSY||r==FL_DISCONNECTED);std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(replacement);
 flChannelInfo after{};CHECK(flQuery(replacement,&after)==FL_OK);CHECK(after.generation>before.generation);
 CHECK(flAcquireBuffer(replacement,&b,100)==FL_OK);CHECK(flSubmit(replacement,&b,222)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK);CHECK(f.ptsNs==222);CHECK(flRelease(c,&f)==FL_OK);
 flClose(replacement);flClose(c);puts("Reconnect refuses held storage, then starts fresh generation");
}
