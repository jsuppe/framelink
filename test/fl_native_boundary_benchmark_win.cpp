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
#define CHECK(x) do{if(!(x)){fprintf(stderr,"pid%lu line%d: %s\n",GetCurrentProcessId(),__LINE__,#x);std::exit(1);}}while(0)
#define FL_TRACE_NS benchtrace
#define FL_TRACE_ENV "FL_BENCH_TRACE"
#include "framelink_trace.hpp"
#include "framelink_memory.h"
#include <chrono>
#include <thread>
#include <fstream>
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
static unsigned frames=3720,warmup=120,fps=60,hops=2,width=1920,height=1080,delayUs=0,delayFrame=180;
static flFormat format=FL_FORMAT_BGRA8;static DXGI_FORMAT dxformat=DXGI_FORMAT_B8G8R8A8_UNORM;static std::string prefix;
static uint64_t nowNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static unsigned option(const char*name,unsigned fallback,unsigned lo,unsigned hi){const char*s=getenv(name);if(!s)return fallback;char*end=nullptr;auto n=strtoul(s,&end,10);CHECK(*s&&end&&!*end&&n>=lo&&n<=hi);return n;}
static void configure(){frames=option("FL_BENCH_FRAMES",3720,2,20000);warmup=option("FL_BENCH_WARMUP",120,0,frames-2);fps=option("FL_BENCH_FPS",60,1,240);hops=option("FL_BENCH_HOPS",2,1,2);width=option("FL_BENCH_WIDTH",1920,32,3840);height=option("FL_BENCH_HEIGHT",1080,16,2160);delayUs=option("FL_BENCH_DELAY_US",0,0,1000000);delayFrame=option("FL_BENCH_DELAY_FRAME",180,0,frames-1);const char*p=getenv("FL_BENCH_OUT");CHECK(p&&*p);prefix=p;const char*f=getenv("FL_BENCH_FORMAT");if(f){CHECK(!strcmp(f,"NV12")||!strcmp(f,"BGRA"));if(!strcmp(f,"NV12")){format=FL_FORMAT_NV12;dxformat=DXGI_FORMAT_NV12;CHECK(!(width%2)&&!(height%2));}}}
struct Header{uint64_t identity,pts,sourceReady,magic;};
struct Observation{uint64_t identity,pts,ready,arrived,retired;};
static int writer(const std::string&name){
 flChannel*p=nullptr;CHECK(flOpenProducer((name+(hops==1?".down":".up")).c_str(),&p)==FL_OK);
 auto ready=event(name+".sourceReady"),go=event(name+".go"),done=event(name+".sourceDone"),exit=event(name+".sourceExit");CHECK(SetEvent(ready));wait(go);
 const auto epoch=nowNs()+100000000;std::vector<std::pair<uint64_t,uint64_t>> waits;waits.reserve(frames);
 for(unsigned n=0;n<frames;n++){
  std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(epoch+uint64_t(n)*1000000000/fps)));
  flBuffer b{};auto before=nowNs();CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);waits.emplace_back(n,nowNs()-before);
  flMappedImage mapped{};CHECK(flImageMapPlanes(b.image,&mapped)==FL_OK);
  for(unsigned i=0;i<mapped.planes;i++)for(unsigned y=0;y<mapped.plane[i].rows;y++)memset((char*)mapped.plane[i].pixels+y*mapped.plane[i].stride,(unsigned char)(n+i),mapped.plane[i].rowBytes);
  Header header{n,uint64_t(n)*1000000000/fps,nowNs(),0x46524c42454e4348ULL};memcpy(mapped.plane[0].pixels,&header,sizeof(header));flImageUnmap(b.image);
  flFrameMetadata m{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(header.pts),uint64_t(n+1)*1000000000/fps-header.pts,77,{}};CHECK(flSetFrameMetadata(p,&b,&m)==FL_OK);CHECK(flSubmit(p,&b,header.pts)==FL_OK);
 }
 CHECK(SetEvent(done));wait(exit);std::ofstream out(prefix+".producer.csv");out<<"identity,acquire_wait_ns\n";for(auto v:waits)out<<v.first<<','<<v.second<<'\n';out.close();CHECK(out.good());flClose(p);for(auto h:{ready,go,done,exit})CloseHandle(h);return 0;
}
static int forward(const std::string&name,HANDLE h0,HANDLE h1){
 void*handles[2]={h0,h1};flChannel*up=nullptr,*down=nullptr;CHECK(flCreateChannelFromD3D11Handles((name+".up").c_str(),width,height,format,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,nullptr,handles,&up)==FL_OK);CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);
 auto ready=event(name+".relayReady"),start=event(name+".relayStart"),initialized=event(name+".relayInitialized"),drained=event(name+".relayDrained"),exit=event(name+".relayExit");CHECK(SetEvent(ready));wait(start);flsample::NativeRelay relay(up,down,same);CHECK(relay.initialize()==FL_OK);CHECK(SetEvent(initialized));
 const auto deadline=GetTickCount64()+uint64_t(frames)*1000/fps+30000;
 for(unsigned n=0;n<frames;){CHECK(GetTickCount64()<deadline);CHECK(relay.collect()==FL_OK);flFrame f{};auto result=flAcquireFrame(up,&f,1);if(result==FL_TIMEOUT)continue;CHECK(result==FL_OK&&f.ptsNs==int64_t(uint64_t(n)*1000000000/fps));CHECK(relay.forward(f)==FL_OK);n++;}
 flResult done=FL_BUSY;while(done==FL_BUSY){CHECK(GetTickCount64()<deadline);done=relay.finish();if(done==FL_BUSY)Sleep(1);}CHECK(done==FL_OK);CHECK(SetEvent(drained));wait(exit);flClose(down);flClose(up);for(auto h:{h0,h1,ready,start,initialized,drained,exit})CloseHandle(h);return 0;
}
int main(int argc,char**argv){configure();if(argc==3&&!strcmp(argv[1],"producer"))return writer(argv[2]);if(argc==5&&!strcmp(argv[1],"relay"))return forward(argv[2],(HANDLE)(uintptr_t)strtoull(argv[3],nullptr,10),(HANDLE)(uintptr_t)strtoull(argv[4],nullptr,10));CHECK(argc==1);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
 D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=dxformat;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ComPtr<ID3D11Texture2D> original[2];HANDLE handles[2]{};for(unsigned i=0;i<2;i++){CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&original[i])));ComPtr<IDXGIResource1> resource;CHECK(SUCCEEDED(original[i].As(&resource)));CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handles[i])));}
 auto name="boundary-bench-"+std::to_string(GetCurrentProcessId());void*reversed[2]={handles[1],handles[0]};flChannel*down=nullptr;CHECK(flCreateChannelFromD3D11Handles((name+".down").c_str(),width,height,format,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,nullptr,reversed,&down)==FL_OK);
 ComPtr<ID3D11Device5> gpu;CHECK(SUCCEEDED(flChannelD3D11Device(down)->QueryInterface(IID_PPV_ARGS(&gpu))));ComPtr<ID3D11DeviceContext> base;gpu->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> context;CHECK(SUCCEEDED(base.As(&context)));
 d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> output;CHECK(SUCCEEDED(gpu->CreateTexture2D(&d,nullptr,&output)));
 ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(gpu->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));HANDLE fenceHandle=nullptr;CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fenceHandle)));
 // Prime driver/private verification allocation on the local creator's device.
 D3D11_TEXTURE2D_DESC primeDesc=d;ComPtr<ID3D11Texture2D> prime;CHECK(SUCCEEDED(device->CreateTexture2D(&primeDesc,nullptr,&prime)));ctx->CopyResource(prime.Get(),original[0].Get());D3D11_MAPPED_SUBRESOURCE primed{};CHECK(SUCCEEDED(ctx->Map(prime.Get(),0,D3D11_MAP_READ,0,&primed)));ctx->Unmap(prime.Get(),0);
 auto sourceReady=event(name+".sourceReady"),go=event(name+".go"),sourceDone=event(name+".sourceDone"),sourceExit=event(name+".sourceExit"),relayReady=event(name+".relayReady"),relayStart=event(name+".relayStart"),relayInitialized=event(name+".relayInitialized"),relayDrained=event(name+".relayDrained"),relayExit=event(name+".relayExit");PROCESS_INFORMATION relay{};
 if(hops==2){relay=spawn("relay "+name+" "+std::to_string((uintptr_t)handles[0])+" "+std::to_string((uintptr_t)handles[1]),{handles[0],handles[1]});wait(relayReady);}auto source=spawn("producer "+name);wait(sourceReady);if(hops==2){CHECK(SetEvent(relayStart));wait(relayInitialized);}CHECK(SetEvent(go));std::vector<Observation> observations;observations.reserve(frames);
 for(unsigned n=0;n<frames;n++){
  flFrame f{};CHECK(flAcquireFrame(down,&f,2000)==FL_OK);auto arrival=nowNs();if(f.ptsNs!=int64_t(uint64_t(n)*1000000000/fps))fprintf(stderr,"expected%u actual_pts%lld\n",n,(long long)f.ptsNs);CHECK(f.ptsNs==int64_t(uint64_t(n)*1000000000/fps));flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&f,&m)==FL_OK&&m.dtsNs==f.ptsNs&&m.durationNs==uint64_t(n+1)*1000000000/fps-f.ptsNs&&m.streamGeneration==77);
  uint64_t lease=0;CHECK(flLeaseFrame(down,&f,&lease)==FL_OK);if(delayUs&&n==delayFrame)std::this_thread::sleep_for(std::chrono::microseconds(delayUs));context->CopyResource(output.Get(),flImageD3D11(f.image));CHECK(SUCCEEDED(context->Signal(fence.Get(),n+1)));context->Flush();CHECK(flCompleteReadLeaseD3D11Fence(down,lease,fenceHandle,n+1)==FL_OK);
  auto until=GetTickCount64()+2000;flResult complete=FL_BUSY;while(complete==FL_BUSY){CHECK(GetTickCount64()<until);complete=flCollectReadLeases(down);if(complete==FL_BUSY)Sleep(1);}CHECK(complete==FL_OK);auto retired=nowNs();D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(SUCCEEDED(context->Map(output.Get(),0,D3D11_MAP_READ,0,&mapped)));Header header{};memcpy(&header,mapped.pData,sizeof(header));CHECK(header.identity==n&&header.pts==uint64_t(f.ptsNs)&&header.magic==0x46524c42454e4348ULL&&arrival>=header.sourceReady);
  CHECK(((unsigned char*)mapped.pData)[mapped.RowPitch*(height-1)+(format==FL_FORMAT_NV12?width-1:(width-1)*4)]==(unsigned char)n);if(format==FL_FORMAT_NV12)CHECK(((unsigned char*)mapped.pData)[mapped.RowPitch*height+mapped.RowPitch*(height/2-1)+width-1]==(unsigned char)(n+1));context->Unmap(output.Get(),0);
  if(benchtrace::enabled()){auto id=benchtrace::id((name+".down").c_str());benchtrace::record({"benchmark.gpu_complete",retired,0,id,f.sequence,f._readyValue,f.slot,f.ptsNs,0});benchtrace::payload("benchmark.private_full_readback",id,uint64_t(width)*height*(format==FL_FORMAT_NV12?3:8)/2,FL_MEMORY_READBACK);}
  observations.push_back({n,header.pts,header.sourceReady,arrival,retired});
 }
 wait(sourceDone);if(hops==2)wait(relayDrained);CHECK(SetEvent(sourceExit));join(source);if(hops==2){CHECK(SetEvent(relayExit));join(relay);}
 std::ofstream out(prefix+".consumer.csv");out<<"identity,pts_ns,source_marker_ns,arrival_ns,retired_ns\n";for(auto&v:observations)out<<v.identity<<','<<v.pts<<','<<v.ready<<','<<v.arrived<<','<<v.retired<<'\n';out.close();CHECK(out.good());double rate=double(frames-warmup-1)*1e9/(observations.back().arrived-observations[warmup].arrived);printf("{\"hops\":%u,\"format\":\"%s\",\"width\":%u,\"height\":%u,\"pool\":2,\"frames\":%u,\"fresh_fps\":%.6f,\"pcm_qualified\":false,\"gpu_copy_consumer\":true}\n",hops,format==FL_FORMAT_NV12?"NV12":"BGRA",width,height,frames,rate);
 flClose(down);for(auto h:{handles[0],handles[1],fenceHandle,sourceReady,go,sourceDone,sourceExit,relayReady,relayStart,relayInitialized,relayDrained,relayExit})CloseHandle(h);return 0;
}
