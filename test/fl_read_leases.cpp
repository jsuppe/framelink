#include "framelink.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
#ifdef _WIN32
#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <wrl/client.h>
struct Completion {
 Microsoft::WRL::ComPtr<ID3D11Fence> fence;
 Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context;
 HANDLE handle=nullptr;
 explicit Completion(flChannel* c) {
  Microsoft::WRL::ComPtr<ID3D11Device5> device;
  CHECK(SUCCEEDED(flChannelD3D11Device(c)->QueryInterface(IID_PPV_ARGS(&device))));
  CHECK(SUCCEEDED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));
  CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&handle)));
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> base;device->GetImmediateContext(&base);
  CHECK(SUCCEEDED(base.As(&context)));
 }
 flResult handoff(flChannel* c,uint64_t t) {
  auto r=flCompleteReadLeaseD3D11Fence(c,t,handle,1);
  if(r==FL_OK){CloseHandle(handle);handle=nullptr;}return r;
 }
 void signal(){CHECK(SUCCEEDED(context->Signal(fence.Get(),1)));context->Flush();}
 ~Completion(){if(handle)CloseHandle(handle);}
};
#else
#include "framelink_sync_fd.h"
#include <sys/eventfd.h>
#include <unistd.h>
struct Completion {
 int fd=-1;
 explicit Completion(flChannel*) {fd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(fd>=0);}
 flResult handoff(flChannel* c,uint64_t t){int borrowed=dup(fd);CHECK(borrowed>=0);auto r=flCompleteReadLeaseSyncFd(c,t,borrowed);close(borrowed);return r;}
 void signal(){uint64_t one=1;CHECK(write(fd,&one,sizeof(one))==sizeof(one));}
 ~Completion(){close(fd);}
};
#endif
static void collect(flChannel* c) {
 for(int i=0;i<1000;++i){auto r=flCollectReadLeases(c);if(r==FL_OK)return;CHECK(r==FL_BUSY||r==FL_DISCONNECTED);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 CHECK(false);
}
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
 // Real D3D shared fences on Windows; deterministic eventfd readiness controls
 // on POSIX (not a claim of GPU sync-file driver qualification).
 Completion early(c),late(c);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,404)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&first)==FL_OK);
 CHECK(early.handoff(c,first)==FL_OK && flCompleteReadLease(c,first)==FL_INVALID);
 CHECK(flRetainReadLease(c,first,&reader)==FL_INVALID);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,505)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&second)==FL_OK);
 CHECK(late.handoff(c,second)==FL_OK);
 late.signal();
 for(int i=0;i<10;++i){CHECK(flCollectReadLeases(c)==FL_BUSY);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 CHECK(flAcquireBuffer(p,&extra,0)==FL_TIMEOUT);
 early.signal();collect(c);
 CHECK(flAcquireBuffer(p,&extra,1000)==FL_OK && flCancelBuffer(p,&extra)==FL_OK);
 // A disconnected producer does not cancel a downstream reader's GPU work.
 Completion restart(c);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,606)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&first)==FL_OK);
 CHECK(restart.handoff(c,first)==FL_OK);
 flClose(p);p=nullptr;std::this_thread::sleep_for(std::chrono::milliseconds(30));
 CHECK(flOpenProducer(name.c_str(),&p)==FL_BUSY && !p);
 CHECK(flCollectReadLeases(c)==FL_BUSY);
 restart.signal();collect(c);
 for(int i=0;i<100&&!p;++i){r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_BUSY||r==FL_DISCONNECTED);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 CHECK(p && flCompleteReadLease(c,first)==FL_INVALID);
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,707)==FL_OK);
 CHECK(flAcquireFrame(c,&f,1000)==FL_OK && flLeaseFrame(c,&f,&second)==FL_OK);
 CHECK(flReadLeaseFrame(c,second,&f,&generation)==FL_OK && f.ptsNs==707);
 CHECK(flCompleteReadLease(c,second)==FL_OK);collect(c);
 flClose(p);flClose(c);
 std::puts("native read leases: retained readers block reuse; ordered retirement and resize pass");
}
