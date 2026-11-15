#include "framelink_d3d11.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(){
 flD3D11DeviceOwner* owner=nullptr;CHECK(flCreateD3D11DeviceOwner(nullptr,&owner)==FL_OK&&owner);
 uint8_t luid[8]{};CHECK(flD3D11DeviceOwnerAdapter(owner,luid)==FL_OK);
 flD3D11DeviceOwner* writer=nullptr;CHECK(flCreateD3D11DeviceOwner(luid,&writer)==FL_OK);
 auto* device=flD3D11DeviceOwnerDevice(owner);CHECK(device);
 std::string base="owner-"+std::to_string(GetCurrentProcessId());
 for(int i=0;i<3;++i){
  auto name=base+std::to_string(i);flChannel*c=nullptr,*p=nullptr,*busy=reinterpret_cast<flChannel*>(1);
  CHECK(flCreateChannelWithD3D11DeviceOwner(owner,name.c_str(),64,32,FL_FORMAT_BGRA8,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
  CHECK(flChannelD3D11Device(c)==device);
  std::thread competitor([&]{CHECK(flCreateChannelWithD3D11DeviceOwner(owner,(name+"busy").c_str(),64,32,FL_FORMAT_BGRA8,2,0,&busy)==FL_BUSY&&busy==nullptr);});competitor.join();
  CHECK(flOpenProducerD3D11BoundedWithDeviceOwner(writer,name.c_str(),1000,nullptr,&p)==FL_OK);
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);void*data=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64*4;++x)static_cast<unsigned char*>(data)[y*stride+x]=static_cast<unsigned char>(i+27);
  flImageUnmap(b.image);CHECK(flSubmit(p,&b,123+i)==FL_OK);
  flFrame frame{};CHECK(flAcquireFrame(c,&frame,1000)==FL_OK&&frame.ptsNs==123+i);
  CHECK(flImageMap(frame.image,&data,&stride)==FL_OK);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64*4;++x)CHECK(static_cast<unsigned char*>(data)[y*stride+x]==i+27);
  flImageUnmap(frame.image);
  uint64_t lease=0;CHECK(flLeaseFrame(c,&frame,&lease)==FL_OK);
  CHECK(flCompleteReadLease(c,lease)==FL_OK);CHECK(flCollectReadLeases(c)==FL_OK);
  flD3D11PoolSnapshot*snapshot=nullptr;CHECK(flSnapshotD3D11ConsumerPool(c,&snapshot)==FL_OK);
  flClose(p);flClose(c);
  CHECK(flCreateChannelWithD3D11DeviceOwner(owner,name.c_str(),64,32,FL_FORMAT_BGRA8,2,0,&busy)==FL_BUSY);
  flReleaseD3D11PoolSnapshot(snapshot);
 }
 // Failed argument/cancel setup must not consume the owner attachment.
 flChannel*p=reinterpret_cast<flChannel*>(1);HANDLE cancelled=CreateEventW(nullptr,TRUE,TRUE,nullptr);
 CHECK(flOpenProducerD3D11BoundedWithDeviceOwner(writer,base.c_str(),1000,cancelled,&p)==FL_DISCONNECTED&&p==nullptr);CloseHandle(cancelled);
 uint8_t missing[8];for(auto&v:missing)v=0xff;flD3D11DeviceOwner*bad=reinterpret_cast<flD3D11DeviceOwner*>(1);
 CHECK(flCreateD3D11DeviceOwner(missing,&bad)==FL_ADAPTER_MISMATCH&&bad==nullptr);
 // Channel retains owner after caller's final release.
 flChannel*c=nullptr;CHECK(flCreateChannelWithD3D11DeviceOwner(owner,base.c_str(),64,32,FL_FORMAT_BGRA8,2,0,&c)==FL_OK);
 flReleaseD3D11DeviceOwner(owner);CHECK(flChannelD3D11Device(c)==device);flClose(c);
 flReleaseD3D11DeviceOwner(writer);
 puts("owner: retained identity, fresh channels/pixels, concurrent busy, snapshot retirement, cancellation rollback and caller release passed");
}
