// Retained-device owner fault contract: unsafe retirement poisons, outstanding
// remote reads keep the owner BUSY until real completion, nothing times out.
#include "framelink_d3d11.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const uint32_t W=64,H=32;
static std::string base;
static flD3D11DeviceOwner* owner(const uint8_t* luid=nullptr){flD3D11DeviceOwner*o=nullptr;CHECK(flCreateD3D11DeviceOwner(luid,&o)==FL_OK&&o);return o;}
static flResult attach(flD3D11DeviceOwner*o,const char*tag,flChannel**out){
 return flCreateChannelWithD3D11DeviceOwner(o,(base+tag).c_str(),W,H,FL_FORMAT_BGRA8,2,FL_MAP_CPU,out);}
static void submit(flChannel*p,int64_t pts){flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);CHECK(flSubmit(p,&b,pts)==FL_OK);}
int main(){
 base="ownerfault-"+std::to_string(GetCurrentProcessId())+"-";
 uint8_t luid[8]{};{auto*o=owner();CHECK(flD3D11DeviceOwnerAdapter(o,luid)==FL_OK);flReleaseD3D11DeviceOwner(o);}

 // 1. Consumer closed while still holding a frame: its owner is poisoned for good.
 //    The producer's owner then waits on a completion that can never arrive: BUSY, never a timeout.
 {auto*co=owner(luid),*po=owner(luid);flChannel*c=nullptr,*p=nullptr;
  CHECK(attach(co,"held",&c)==FL_OK);
  CHECK(flOpenProducerD3D11BoundedWithDeviceOwner(po,(base+"held").c_str(),1000,nullptr,&p)==FL_OK);
  submit(p,1);flFrame f{};CHECK(flAcquireFrame(c,&f,1000)==FL_OK);
  flClose(c);                                   // frame never released
  flChannel*again=reinterpret_cast<flChannel*>(1);
  CHECK(attach(co,"held2",&again)==FL_DISCONNECTED&&again==nullptr);
  CHECK(attach(co,"held3",&again)==FL_DISCONNECTED&&again==nullptr);   // stays poisoned
  flClose(p);                                   // remote read outstanding, reader is gone
  CHECK(attach(po,"held4",&again)==FL_BUSY&&again==nullptr);
  Sleep(300);
  CHECK(attach(po,"held5",&again)==FL_BUSY&&again==nullptr);          // no timeout fabricates retirement
  flReleaseD3D11DeviceOwner(co);flReleaseD3D11DeviceOwner(po);}

 // 2. Producer closed while a live consumer still reads its frame: BUSY until the
 //    consumer really releases, then the same owner attaches again.
 {auto*co=owner(luid),*po=owner(luid);flChannel*c=nullptr,*p=nullptr;
  CHECK(attach(co,"live",&c)==FL_OK);
  CHECK(flOpenProducerD3D11BoundedWithDeviceOwner(po,(base+"live").c_str(),1000,nullptr,&p)==FL_OK);
  submit(p,2);flFrame f{};CHECK(flAcquireFrame(c,&f,1000)==FL_OK&&f.ptsNs==2);
  flClose(p);
  flChannel*next=reinterpret_cast<flChannel*>(1);
  CHECK(attach(po,"live2",&next)==FL_BUSY&&next==nullptr);
  CHECK(flRelease(c,&f)==FL_OK);                // real completion
  flResult r=FL_BUSY;for(int i=0;i<400&&r==FL_BUSY;i++){r=attach(po,"live3",&next);if(r==FL_BUSY)Sleep(5);}
  CHECK(r==FL_OK&&next);flClose(next);
  flClose(c);
  CHECK(attach(co,"live4",&next)==FL_OK);flClose(next);               // clean consumer close did not poison
  flReleaseD3D11DeviceOwner(co);flReleaseD3D11DeviceOwner(po);}

 // 3. Producer closed in the middle of a submit: its owner is poisoned.
 {auto*co=owner(luid),*po=owner(luid);flChannel*c=nullptr,*p=nullptr;
  CHECK(attach(co,"mid",&c)==FL_OK);
  CHECK(flOpenProducerD3D11BoundedWithDeviceOwner(po,(base+"mid").c_str(),1000,nullptr,&p)==FL_OK);
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);uint64_t signal=0;CHECK(flBeginSubmit(p,&b,&signal)==FL_OK);
  flClose(p);
  flChannel*again=reinterpret_cast<flChannel*>(1);
  CHECK(flOpenProducerD3D11BoundedWithDeviceOwner(po,(base+"mid").c_str(),1000,nullptr,&again)==FL_DISCONNECTED&&again==nullptr);
  flClose(c);
  flReleaseD3D11DeviceOwner(co);flReleaseD3D11DeviceOwner(po);}

 puts("owner faults: held-frame close poisons, dead reader stays BUSY, live reader release retires, mid-submit close poisons");
}
