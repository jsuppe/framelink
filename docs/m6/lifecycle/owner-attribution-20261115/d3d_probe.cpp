// Diagnostic only, no FrameLink: what on a persistent device retains heap per cycle?
// d3d_probe <variant> <cycles> <out.csv>
// staging   : create+release 2 staging textures
// stagemap  : + Map/Unmap 150x each (no copies)
// fullshared: + CopyResource to/from textures opened from another device (NT shared)
// fulllocal : + CopyResource with local DEFAULT textures (not shared)
// opened    : open+release shared textures only
// fullflush : fullshared + ClearState/Flush AFTER releasing resources
#include <windows.h>
#include <psapi.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(2);}}while(0)
static unsigned long long heapBusy(){unsigned long long t=0;std::vector<HANDLE> h(256);DWORD n=GetProcessHeaps(256,h.data());
 for(DWORD i=0;i<n&&i<256;i++){HeapLock(h[i]);PROCESS_HEAP_ENTRY e{};while(HeapWalk(h[i],&e))if(e.wFlags&PROCESS_HEAP_ENTRY_BUSY)t+=e.cbData;HeapUnlock(h[i]);}return t/1024;}
static unsigned long long priv(){PROCESS_MEMORY_COUNTERS_EX m{};K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m);return m.PrivateUsage/1024;}
static ComPtr<ID3D11Device> dev(ComPtr<ID3D11DeviceContext>&ctx){ComPtr<ID3D11Device> d;D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_1;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&fl,1,D3D11_SDK_VERSION,&d,nullptr,&ctx)));return d;}
int main(int argc,char**argv){CHECK(argc==4);std::string v=argv[1];int cycles=atoi(argv[2]);if(v=="long")v="fulllocal";FILE*out=fopen(argv[3],"w");CHECK(out);
 fprintf(out,"cycle,priv_kib,heap_busy_kib\n");
 ComPtr<ID3D11DeviceContext> ctx,ctxB;auto A=dev(ctx);auto B=dev(ctxB);ComPtr<ID3D11Device1> A1;CHECK(SUCCEEDED(A.As(&A1)));
 D3D11_TEXTURE2D_DESC td{};td.Width=1920;td.Height=1080;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;
 td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 auto sd=td;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
 fprintf(out,"-1,%llu,%llu\n",priv(),heapBusy());
 for(int c=0;c<cycles;c++){
  {ComPtr<ID3D11Texture2D> tex[2],stg[2];std::vector<HANDLE> handles;
   if(v=="fullshared"||v=="opened"||v=="fullflush"){
    auto shared=td;shared.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    for(int i=0;i<2;i++){ComPtr<ID3D11Texture2D> t;CHECK(SUCCEEDED(B->CreateTexture2D(&shared,nullptr,&t)));ComPtr<IDXGIResource1> r;CHECK(SUCCEEDED(t.As(&r)));HANDLE h=nullptr;
     CHECK(SUCCEEDED(r->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&h)));handles.push_back(h);
     CHECK(SUCCEEDED(A1->OpenSharedResource1(h,IID_PPV_ARGS(&tex[i]))));}
   }else if(v=="fulllocal"){for(int i=0;i<2;i++)CHECK(SUCCEEDED(A->CreateTexture2D(&td,nullptr,&tex[i])));}
   if(v=="staging"||v=="stagemap"||v=="fulllocal"||v=="fullshared"||v=="fullflush")for(int i=0;i<2;i++)CHECK(SUCCEEDED(A->CreateTexture2D(&sd,nullptr,&stg[i])));
   static ComPtr<ID3D11Texture2D> keepTex[2],keepStg[2];static std::vector<char> cpu(1920*1080*4);
   if(v=="keepboth"||v=="keepstg"||v=="keeptex"||v=="down"||v=="up"||v=="copies1"||v=="update"){
    for(int i=0;i<2;i++){
     if(v=="keepstg"||v=="keepboth"){if(!keepStg[i])CHECK(SUCCEEDED(A->CreateTexture2D(&sd,nullptr,&keepStg[i])));stg[i]=keepStg[i];}
     if(v=="keeptex"||v=="keepboth"){if(!keepTex[i])CHECK(SUCCEEDED(A->CreateTexture2D(&td,nullptr,&keepTex[i])));tex[i]=keepTex[i];}
     if(!tex[i])CHECK(SUCCEEDED(A->CreateTexture2D(&td,nullptr,&tex[i])));
     if(!stg[i]&&v!="update")CHECK(SUCCEEDED(A->CreateTexture2D(&sd,nullptr,&stg[i])));}
    const int n=v=="copies1"?1:150;
    for(int k=0;k<n;k++)for(int i=0;i<2;i++){
     if(v=="update"){cpu[0]=char(k);ctx->UpdateSubresource(tex[i].Get(),0,nullptr,cpu.data(),1920*4,0);continue;}
     if(v!="up")ctx->CopyResource(stg[i].Get(),tex[i].Get());
     D3D11_MAPPED_SUBRESOURCE m{};CHECK(SUCCEEDED(ctx->Map(stg[i].Get(),0,D3D11_MAP_READ_WRITE,0,&m)));((char*)m.pData)[0]=char(k);ctx->Unmap(stg[i].Get(),0);
     if(v!="down")ctx->CopyResource(tex[i].Get(),stg[i].Get());}
   }
   else if(v!="staging"&&v!="opened")for(int k=0;k<150;k++)for(int i=0;i<2;i++){
    if(tex[i])ctx->CopyResource(stg[i].Get(),tex[i].Get());
    D3D11_MAPPED_SUBRESOURCE m{};CHECK(SUCCEEDED(ctx->Map(stg[i].Get(),0,D3D11_MAP_READ_WRITE,0,&m)));((char*)m.pData)[0]=char(k);ctx->Unmap(stg[i].Get(),0);
    if(tex[i])ctx->CopyResource(tex[i].Get(),stg[i].Get());}
   if(v!="fullflush"){ctx->ClearState();ctx->Flush();}
   for(auto h:handles)CloseHandle(h);
  }
  if(v=="fullflush"){ctx->ClearState();ctx->Flush();}
  ctxB->Flush();Sleep(50);
  fprintf(out,"%d,%llu,%llu\n",c,priv(),heapBusy());fflush(out);}
 for(int s=0;s<3;s++){Sleep(5000);fprintf(out,"%d,%llu,%llu\n",1000+s,priv(),heapBusy());}
 fclose(out);return 0;}
