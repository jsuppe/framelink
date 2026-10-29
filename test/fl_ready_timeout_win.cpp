#include "framelink.h"
#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 flChannel *c=nullptr,*p=nullptr;
 CHECK(flCreateChannel("ready-timeout-test",64,64,FL_FORMAT_BGRA8,1,&c)==FL_OK);
 for(int i=0;i<100 && !p;i++){flOpenProducer("ready-timeout-test",&p);std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(p);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,100)==FL_OK);
 uint64_t value=0;CHECK(flBeginSubmit(p,&b,&value)==FL_OK);
 CHECK(flEndSubmit(p,&b,value,123)==FL_OK); // deliberately not GPU-ready
 flFrame f{};auto start=std::chrono::steady_clock::now();
 CHECK(flAcquireFrame(c,&f,20)==FL_TIMEOUT);
 CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250));
 void* handle=nullptr;CHECK(flSharedReadyFence(p,&handle)==FL_OK);
 ComPtr<ID3D11Device5> dev;CHECK(SUCCEEDED(flChannelD3D11Device(p)->QueryInterface(IID_PPV_ARGS(&dev))));
 ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(dev->OpenSharedFence(handle,IID_PPV_ARGS(&fence))));
 ComPtr<ID3D11DeviceContext> base;dev->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> ctx;CHECK(SUCCEEDED(base.As(&ctx)));
 CHECK(SUCCEEDED(ctx->Signal(fence.Get(),value)));ctx->Flush();
 CHECK(flAcquireFrame(c,&f,2000)==FL_OK);CHECK(f.ptsNs==123);CHECK(flRelease(c,&f)==FL_OK);
 flClose(p);flClose(c);puts("Unsignaled ready fence times out; later signal preserves pending frame");
}
