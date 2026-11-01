#include "../samples/native_relay.h"
#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"pid%lu line%d: %s\n",GetCurrentProcessId(),__LINE__,#x);exit(1);}}while(0)
static constexpr unsigned Frames=16;
static bool same(flImage*a,flImage*b){using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);auto compare=(Compare)GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles");return compare&&compare(flImageSharedHandle(a),flImageSharedHandle(b))!=FALSE;}
static HANDLE event(const std::string& name){auto h=CreateEventA(nullptr,FALSE,FALSE,name.c_str());CHECK(h);return h;}
static void wait(HANDLE h){CHECK(WaitForSingleObject(h,5000)==WAIT_OBJECT_0);}
static PROCESS_INFORMATION spawn(const std::string& args,const std::vector<HANDLE>& handles={}){
 char exe[MAX_PATH]{};CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));std::string command="\""+std::string(exe)+"\" "+args;
 STARTUPINFOEXA startup{};startup.StartupInfo.cb=handles.empty()?sizeof(STARTUPINFOA):sizeof(startup);SIZE_T bytes=0;std::vector<unsigned char> storage;
 if(!handles.empty()){
  InitializeProcThreadAttributeList(nullptr,1,0,&bytes);storage.resize(bytes);startup.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();CHECK(InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes));
  for(auto h:handles)CHECK(SetHandleInformation(h,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT));
  CHECK(UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,(void*)handles.data(),handles.size()*sizeof(HANDLE),nullptr,nullptr));
 }
 PROCESS_INFORMATION p{};CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,!handles.empty(),CREATE_NO_WINDOW|(handles.empty()?0:EXTENDED_STARTUPINFO_PRESENT),nullptr,nullptr,&startup.StartupInfo,&p));
 if(!handles.empty()){for(auto h:handles)CHECK(SetHandleInformation(h,HANDLE_FLAG_INHERIT,0));DeleteProcThreadAttributeList(startup.lpAttributeList);}return p;
}
static void join(PROCESS_INFORMATION& p){wait(p.hProcess);DWORD code=1;CHECK(GetExitCodeProcess(p.hProcess,&code)&&code==0);CloseHandle(p.hThread);CloseHandle(p.hProcess);}
static int producer(const std::string&name){
 flChannel*p=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&p)==FL_OK);
 auto finished=event(name+".finished");auto started=event(name+".started"),held=event(name+".held"),checked=event(name+".checked");CHECK(SetEvent(started));
 flBuffer b[2]{};for(auto&v:b)CHECK(flAcquireBuffer(p,&v,1000)==FL_OK);
 for(unsigned n=0;n<Frames;n++){
  auto&v=b[n%2];flMappedImage m{};CHECK(flImageMapPlanes(v.image,&m)==FL_OK);
  for(unsigned plane=0;plane<m.planes;plane++)for(unsigned y=0;y<m.plane[plane].rows;y++)memset((char*)m.plane[plane].pixels+y*m.plane[plane].stride,20+n+plane,m.plane[plane].rowBytes);flImageUnmap(v.image);
  flFrameMetadata metadata{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,100+int64_t(n),10000000+n,77,{}};CHECK(flSetFrameMetadata(p,&v,&metadata)==FL_OK);CHECK(flSubmit(p,&v,1000+n)==FL_OK);
  wait(held);flBuffer premature{};CHECK(flAcquireBuffer(p,&premature,0)==FL_TIMEOUT);CHECK(SetEvent(checked));
  flBuffer returned{};CHECK(flAcquireBuffer(p,&returned,5000)==FL_OK&&same(v.image,returned.image));v=returned;
 }
 for(auto&v:b)CHECK(flCancelBuffer(p,&v)==FL_OK);CHECK(SetEvent(finished));CloseHandle(finished);flClose(p);CloseHandle(started);CloseHandle(held);CloseHandle(checked);return 0;
}
static int relay(const std::string&name,HANDLE h0,HANDLE h1){
 void*handles[2]={h0,h1};flChannel*up=nullptr,*down=nullptr;
 CHECK(flCreateChannelFromD3D11Handles((name+".up").c_str(),64,32,FL_FORMAT_NV12,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,nullptr,handles,&up)==FL_OK);
 CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);
 auto finished=event(name+".finished");auto ready=event(name+".ready"),started=event(name+".started");CHECK(SetEvent(ready));wait(started);
 flsample::NativeRelay r(up,down,same);CHECK(r.initialize()==FL_OK);auto deadline=GetTickCount64()+10000;unsigned frames=0;
 while(frames<Frames){CHECK(GetTickCount64()<deadline);CHECK(r.collect()==FL_OK);flFrame f{};auto result=flAcquireFrame(up,&f,1);if(result==FL_TIMEOUT)continue;CHECK(result==FL_OK&&f.ptsNs==1000+frames);CHECK(r.forward(f)==FL_OK);++frames;}
 flResult done=FL_BUSY;while(done==FL_BUSY){CHECK(GetTickCount64()<deadline);done=r.finish();if(done==FL_BUSY)Sleep(1);}CHECK(done==FL_OK);
 wait(finished);CloseHandle(finished);flClose(down);flClose(up);CloseHandle(h0);CloseHandle(h1);CloseHandle(ready);CloseHandle(started);return 0;
}
int main(int argc,char**argv){
 if(argc==3&&!strcmp(argv[1],"producer"))return producer(argv[2]);
 if(argc==5&&!strcmp(argv[1],"relay"))return relay(argv[2],(HANDLE)(uintptr_t)strtoull(argv[3],nullptr,10),(HANDLE)(uintptr_t)strtoull(argv[4],nullptr,10));
 CHECK(argc==1);ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
 D3D11_TEXTURE2D_DESC d{};d.Width=64;d.Height=32;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_NV12;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ComPtr<ID3D11Texture2D> original[2];HANDLE handles[2]{};
 for(unsigned i=0;i<2;i++){CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&original[i])));ComPtr<IDXGIResource1> resource;CHECK(SUCCEEDED(original[i].As(&resource)));CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handles[i])));}
 auto name="relay-process-"+std::to_string(GetCurrentProcessId());auto finished=event(name+".finished");auto ready=event(name+".ready"),started=event(name+".started"),held=event(name+".held"),checked=event(name+".checked");
 void*reversed[2]={handles[1],handles[0]};flChannel*down=nullptr;CHECK(flCreateChannelFromD3D11Handles((name+".down").c_str(),64,32,FL_FORMAT_NV12,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,nullptr,reversed,&down)==FL_OK);
 auto relayProcess=spawn("relay "+name+" "+std::to_string((uintptr_t)handles[0])+" "+std::to_string((uintptr_t)handles[1]),{handles[0],handles[1]});wait(ready);auto writer=spawn("producer "+name);
 ComPtr<ID3D11Device5> gpu;CHECK(SUCCEEDED(flChannelD3D11Device(down)->QueryInterface(IID_PPV_ARGS(&gpu))));ComPtr<ID3D11DeviceContext> base;gpu->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> context;CHECK(SUCCEEDED(base.As(&context)));
 d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> output;CHECK(SUCCEEDED(gpu->CreateTexture2D(&d,nullptr,&output)));
 ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(gpu->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));HANDLE fenceHandle=nullptr;CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fenceHandle)));
 for(unsigned n=0;n<Frames;n++){
  flFrame f{};CHECK(flAcquireFrame(down,&f,5000)==FL_OK&&f.ptsNs==1000+n);flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&f,&m)==FL_OK&&m.dtsNs==100+n&&m.durationNs==10000000+n&&m.streamGeneration==77);
  uint64_t lease=0;CHECK(flLeaseFrame(down,&f,&lease)==FL_OK);CHECK(SetEvent(held));wait(checked);
  context->CopyResource(output.Get(),flImageD3D11(f.image));CHECK(SUCCEEDED(context->Signal(fence.Get(),n+1)));context->Flush();CHECK(flCompleteReadLeaseD3D11Fence(down,lease,fenceHandle,n+1)==FL_OK);
  auto until=GetTickCount64()+3000;flResult complete=FL_BUSY;while(complete==FL_BUSY){CHECK(GetTickCount64()<until);complete=flCollectReadLeases(down);if(complete==FL_BUSY)Sleep(1);}CHECK(complete==FL_OK);
  D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(SUCCEEDED(context->Map(output.Get(),0,D3D11_MAP_READ,0,&mapped)));
  for(unsigned p=0;p<2;p++)for(unsigned y=0;y<(p?16:32);y++)for(unsigned x=0;x<64;x++)CHECK(((unsigned char*)mapped.pData)[(p?mapped.RowPitch*32:0)+mapped.RowPitch*y+x]==20+n+p);context->Unmap(output.Get(),0);
 }
 join(writer);join(relayProcess);flClose(down);for(auto h:handles)CloseHandle(h);for(auto h:{ready,started,held,checked,finished,fenceHandle})CloseHandle(h);
 puts("three-process NV12 relay16/16: exact pixels/PTS/DTS/duration, reversed pool identity, no upstream reuse while downstream held, GPU-fence completion and bounded drain passed");
}
