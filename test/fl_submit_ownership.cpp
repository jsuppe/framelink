#include "framelink.h"
#ifndef _WIN32
#include "framelink_sync_fd.h"
#else
#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <wrl/client.h>
#endif
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
static void signalReady(flChannel* p,uint64_t value) {
#ifdef _WIN32
 using Microsoft::WRL::ComPtr;
 ComPtr<ID3D11Device5> device;CHECK(SUCCEEDED(flChannelD3D11Device(p)->QueryInterface(IID_PPV_ARGS(&device))));
 void* handle=nullptr;CHECK(flSharedReadyFence(p,&handle)==FL_OK);
 ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(device->OpenSharedFence(handle,IID_PPV_ARGS(&fence))));
 ComPtr<ID3D11DeviceContext4> ctx;
 // Query the exact interface rather than assuming the inherited context supports it.
 ComPtr<ID3D11DeviceContext> base;device->GetImmediateContext(&base);CHECK(SUCCEEDED(base.As(&ctx)));
 CHECK(SUCCEEDED(ctx->Signal(fence.Get(),value)));ctx->Flush();
#else
 (void)p;(void)value; // Test has no outstanding GPU writes.
#endif
}
int main() {
 std::string name="ownership-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 flChannel *c=nullptr,*p=nullptr;
 auto created=flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c);
 if(created!=FL_OK){std::fprintf(stderr,"allocation unavailable: %s\n",flResultString(created));return 77;}
 for(int n=0;n<100&&!p;n++){auto r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_NOT_FOUND);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 CHECK(p);
 flBuffer a{},b{},extra{};CHECK(flAcquireBuffer(p,&a,1000)==FL_OK);CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
 CHECK(a.slot!=b.slot);CHECK(flAcquireBuffer(p,&extra,0)==FL_TIMEOUT);
 CHECK(flEndSubmit(p,&a,1,0)==FL_INVALID); // No matching begin.
 flBuffer foreign=a;foreign.image=b.image;uint64_t untouched=123;
 CHECK(flBeginSubmit(p,&foreign,&untouched)==FL_INVALID);CHECK(untouched==123);
 foreign.slot=FL_MAX_POOL_SIZE;CHECK(flBeginSubmit(p,&foreign,&untouched)==FL_INVALID);CHECK(untouched==123);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_BUSY);
 uint64_t av=0;CHECK(flBeginSubmit(p,&a,&av)==FL_OK);CHECK(av==1);
 CHECK(flBeginSubmit(p,&b,&untouched)==FL_BUSY);CHECK(untouched==123);
 CHECK(flBeginSubmit(p,&a,&untouched)==FL_BUSY);CHECK(untouched==123);
 CHECK(flCancelBuffer(p,&a)==FL_BUSY);
 CHECK(flEndSubmit(p,&b,av,0)==FL_INVALID);CHECK(flEndSubmit(p,&a,0,0)==FL_INVALID);
 CHECK(flEndSubmit(p,&a,av+1,0)==FL_INVALID);
 signalReady(p,av);CHECK(flEndSubmit(p,&a,av,101)==FL_OK);
 CHECK(flEndSubmit(p,&a,av,999)==FL_INVALID);CHECK(flBeginSubmit(p,&a,&untouched)==FL_INVALID);
#ifndef _WIN32
 CHECK(flSubmitSyncFd(p,&a,-1,999)==FL_INVALID);
#endif
 flFrame f{};CHECK(flAcquireFrame(c,&f,1000)==FL_OK);CHECK(f.ptsNs==101);
 CHECK(flAcquireBuffer(p,&extra,0)==FL_TIMEOUT);CHECK(flRelease(c,&f)==FL_OK);
 CHECK(flAcquireBuffer(p,&extra,1000)==FL_OK);CHECK(extra.slot==a.slot);
 CHECK(flEndSubmit(p,&a,av,999)==FL_INVALID); // Previous transaction cannot retire new reservation.
 CHECK(flSubmit(p,&extra,202)==FL_OK);CHECK(flAcquireFrame(c,&f,1000)==FL_OK);CHECK(f.ptsNs==202);CHECK(flRelease(c,&f)==FL_OK);
#ifndef _WIN32
 CHECK(flSubmitSyncFd(p,&b,-1,303)==FL_OK);CHECK(flSubmitSyncFd(p,&b,-1,999)==FL_INVALID);
#else
 CHECK(flSubmit(p,&b,303)==FL_OK);
#endif
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK);CHECK(f.ptsNs==303);CHECK(flRelease(c,&f)==FL_OK);
 CHECK(flAcquireBuffer(p,&extra,1000)==FL_OK);
 CHECK(flCancelBuffer(p,&extra)==FL_OK);CHECK(flCancelBuffer(p,&extra)==FL_INVALID);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 flChannelInfo resized{};CHECK(flQuery(p,&resized)==FL_OK);CHECK(resized.width==128);
 flClose(p);flClose(c);p=nullptr;c=nullptr;
 name+="-held";
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,1,FL_MAP_CPU,&c)==FL_OK);
 for(int n=0;n<100&&!p;n++){auto r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_NOT_FOUND);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 CHECK(p);CHECK(flAcquireBuffer(p,&a,1000)==FL_OK);CHECK(flSubmit(p,&a,404)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK);
 flChannelInfo before{},after{};CHECK(flQuery(p,&before)==FL_OK);
#ifdef _WIN32
 DWORD handlesBefore=0,handlesAfter=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore));
#endif
 for(int n=0;n<20;n++)CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);
 CHECK(flQuery(p,&after)==FL_OK);CHECK(after.generation==before.generation && after.width==64);
#ifdef _WIN32
 CHECK(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter));CHECK(handlesAfter<=handlesBefore+2);
#endif
 CHECK(flAcquireBuffer(p,&extra,0)==FL_TIMEOUT); // Refused resize cannot free held storage.
 CHECK(flRelease(c,&f)==FL_OK);CHECK(flAcquireBuffer(p,&a,1000)==FL_OK);CHECK(flCancelBuffer(p,&a)==FL_OK);
 // Successful replacements must close the old duplicated fence handles too.
 for(int n=0;n<16;n++)CHECK(flRequestGeometry(p,n%2?64:128,32,FL_FORMAT_RGBA8)==FL_OK);
#ifdef _WIN32
 CHECK(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter));std::printf("handles before=%lu after=%lu\n",handlesBefore,handlesAfter);
 std::this_thread::sleep_for(std::chrono::milliseconds(200));
 CHECK(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter));std::printf("handles after drain=%lu\n",handlesAfter);CHECK(handlesAfter<=handlesBefore+4);
#endif
 flClose(p);flClose(c);
 puts("Producer reservations: wrong image/slot, duplicate/unmatched begin/end, forged values, geometry retention and post-release reuse passed");
}
