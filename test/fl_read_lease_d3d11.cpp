#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Microsoft::WRL::ComPtr;
static int produce(const char* name,bool crash) {
 flChannel* p=nullptr;CHECK(flOpenProducer(name,&p)==FL_OK);flBuffer b{},spare{},returned{};
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flAcquireBuffer(p,&spare,1000)==FL_OK);
 void* data=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto* v=(unsigned char*)data+y*stride+x*4;v[0]=17;v[1]=91;v[2]=203;v[3]=255;}
 flImageUnmap(b.image);CHECK(flSubmit(p,&b,123456)==FL_OK);
 if(crash){TerminateProcess(GetCurrentProcess(),99);CHECK(false);}
 CHECK(flAcquireBuffer(p,&returned,5000)==FL_OK && returned.slot==b.slot);
 CHECK(flCancelBuffer(p,&spare)==FL_OK && flCancelBuffer(p,&returned)==FL_OK);flClose(p);return 0;
}
int main(int argc,char** argv) {
 if(argc==3)return produce(argv[2],!strcmp(argv[1],"--producer-crash"));
 bool crash=argc==2 && !strcmp(argv[1],"--producer-death");
 bool delayed=argc==2 && !strcmp(argv[1],"--delayed-completion");
 std::string name="d3d-lease-"+std::to_string(GetCurrentProcessId());flChannel* c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
 char exe[MAX_PATH]{};CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));
 std::string command="\""+std::string(exe)+"\" "+(crash?"--producer-crash ":"--producer ")+name;
 STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
 flFrame f{};uint64_t lease=0;CHECK(flAcquireFrame(c,&f,5000)==FL_OK && f.ptsNs==123456 && flLeaseFrame(c,&f,&lease)==FL_OK);
 ComPtr<ID3D11Device5> device;CHECK(SUCCEEDED(flChannelD3D11Device(c)->QueryInterface(IID_PPV_ARGS(&device))));
 ComPtr<ID3D11DeviceContext> base;device->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> context;CHECK(SUCCEEDED(base.As(&context)));
 D3D11_TEXTURE2D_DESC desc{};flImageD3D11(f.image)->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> output;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&output)));
 ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));
 HANDLE shared=nullptr;CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared)));
 // Delay actual GPU execution with a queue-side wait. A distinct device on
 // the same adapter signals it, avoiding a wait/signal deadlock on one queue.
 ComPtr<ID3D11Fence> gate,signalGate;ComPtr<ID3D11DeviceContext4> signalContext;
 ComPtr<ID3D11Device> signalDevice;
 if(delayed){
  CHECK(SUCCEEDED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&gate))));
  HANDLE gateHandle=nullptr;CHECK(SUCCEEDED(gate->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&gateHandle)));
  ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;
  CHECK(SUCCEEDED(device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter)));
  D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_1;
  CHECK(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&level,1,D3D11_SDK_VERSION,&signalDevice,nullptr,nullptr)));
  ComPtr<ID3D11Device5> dev5;CHECK(SUCCEEDED(signalDevice.As(&dev5)));
  CHECK(SUCCEEDED(dev5->OpenSharedFence(gateHandle,IID_PPV_ARGS(&signalGate))));CloseHandle(gateHandle);
  ComPtr<ID3D11DeviceContext> signalBase;signalDevice->GetImmediateContext(&signalBase);CHECK(SUCCEEDED(signalBase.As(&signalContext)));
  CHECK(SUCCEEDED(context->Wait(gate.Get(),1)));
 }
 context->CopyResource(output.Get(),flImageD3D11(f.image));CHECK(SUCCEEDED(context->Signal(fence.Get(),1)));context->Flush();
 CHECK(flCompleteReadLeaseD3D11Fence(c,lease,shared,1)==FL_OK);CloseHandle(shared);
 if(delayed){
  // Producer consumed the spare slot already: it cannot finish until the
  // consumer completion retires. Check both fence and transport ownership.
  for(unsigned i=0;i<20;++i){CHECK(fence->GetCompletedValue()==0);CHECK(flCollectReadLeases(c)==FL_BUSY);CHECK(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT);Sleep(1);}
  CHECK(SUCCEEDED(signalContext->Signal(signalGate.Get(),1)));signalContext->Flush();
 }
 bool collected=false;for(int i=0;i<2000;++i){auto r=flCollectReadLeases(c);if(r==FL_OK){collected=true;break;}CHECK(r==FL_BUSY);Sleep(1);}CHECK(collected);
 D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(SUCCEEDED(context->Map(output.Get(),0,D3D11_MAP_READ,0,&mapped)));
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto* v=(unsigned char*)mapped.pData+y*mapped.RowPitch+x*4;CHECK(v[0]==17&&v[1]==91&&v[2]==203&&v[3]==255);}
 context->Unmap(output.Get(),0);
 CHECK(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0);DWORD result=1;CHECK(GetExitCodeProcess(process.hProcess,&result)&&result==(crash?99u:0u));CloseHandle(process.hThread);CloseHandle(process.hProcess);
 flClose(c);puts("separate-process D3D11 shared-allocation GPU read, fence lease retirement and all-pixel verification passed");
}
