#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Microsoft::WRL::ComPtr;
static int produce(const char* name) {
 flChannel* p=nullptr;CHECK(flOpenProducer(name,&p)==FL_OK);flBuffer b{},spare{},returned{};
 CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flAcquireBuffer(p,&spare,1000)==FL_OK);
 void* data=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto* v=(unsigned char*)data+y*stride+x*4;v[0]=17;v[1]=91;v[2]=203;v[3]=255;}
 flImageUnmap(b.image);CHECK(flSubmit(p,&b,123456)==FL_OK);
 CHECK(flAcquireBuffer(p,&returned,5000)==FL_OK && returned.slot==b.slot);
 CHECK(flCancelBuffer(p,&spare)==FL_OK && flCancelBuffer(p,&returned)==FL_OK);flClose(p);return 0;
}
int main(int argc,char** argv) {
 if(argc==3 && !strcmp(argv[1],"--producer"))return produce(argv[2]);
 std::string name="d3d-lease-"+std::to_string(GetCurrentProcessId());flChannel* c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
 char exe[MAX_PATH]{};CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));
 std::string command="\""+std::string(exe)+"\" --producer "+name;
 STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
 flFrame f{};uint64_t lease=0;CHECK(flAcquireFrame(c,&f,5000)==FL_OK && f.ptsNs==123456 && flLeaseFrame(c,&f,&lease)==FL_OK);
 ComPtr<ID3D11Device5> device;CHECK(SUCCEEDED(flChannelD3D11Device(c)->QueryInterface(IID_PPV_ARGS(&device))));
 ComPtr<ID3D11DeviceContext> base;device->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> context;CHECK(SUCCEEDED(base.As(&context)));
 D3D11_TEXTURE2D_DESC desc{};flImageD3D11(f.image)->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> output;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&output)));
 ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));
 HANDLE shared=nullptr;CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared)));
 context->CopyResource(output.Get(),flImageD3D11(f.image));CHECK(SUCCEEDED(context->Signal(fence.Get(),1)));context->Flush();
 CHECK(flCompleteReadLeaseD3D11Fence(c,lease,shared,1)==FL_OK);CloseHandle(shared);
 bool collected=false;for(int i=0;i<2000;++i){auto r=flCollectReadLeases(c);if(r==FL_OK){collected=true;break;}CHECK(r==FL_BUSY);Sleep(1);}CHECK(collected);
 D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(SUCCEEDED(context->Map(output.Get(),0,D3D11_MAP_READ,0,&mapped)));
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto* v=(unsigned char*)mapped.pData+y*mapped.RowPitch+x*4;CHECK(v[0]==17&&v[1]==91&&v[2]==203&&v[3]==255);}
 context->Unmap(output.Get(),0);
 CHECK(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0);DWORD result=1;CHECK(GetExitCodeProcess(process.hProcess,&result)&&result==0);CloseHandle(process.hThread);CloseHandle(process.hProcess);
 flClose(c);puts("separate-process D3D11 shared-allocation GPU read, fence lease retirement and all-pixel verification passed");
}
