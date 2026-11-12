#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool sample(const char* phase,unsigned n,ULONG refs){
 PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;if(!K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof p))return false;
 DWORD handles=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handles))return false;
 unsigned long long priv=0,mapped=0,img=0,reserve=0; MEMORY_BASIC_INFORMATION m{}; uintptr_t addr=0;
 while(VirtualQuery((void*)addr,&m,sizeof m)==sizeof m){if(m.State==MEM_COMMIT){if(m.Type==MEM_PRIVATE)priv+=m.RegionSize;else if(m.Type==MEM_MAPPED)mapped+=m.RegionSize;else if(m.Type==MEM_IMAGE)img+=m.RegionSize;}else if(m.State==MEM_RESERVE)reserve+=m.RegionSize;auto next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=addr)break;addr=next;}
 printf("%s,%u,%llu,%llu,%lu,%lu,%llu,%llu,%llu,%llu\n",phase,n,GetTickCount64(),(unsigned long long)p.PrivateUsage,handles,refs,priv,mapped,img,reserve);fflush(stdout);return true;
}
int main(int argc,char**argv){CHECK(argc==3);bool warp=!strcmp(argv[1],"warp");bool textures=!strcmp(argv[2],"textures");CHECK(warp||!strcmp(argv[1],"hardware"));CHECK(textures||!strcmp(argv[2],"empty"));
 puts("phase,cycle,tick_ms,private_bytes,handles,device_release_refs,committed_private,committed_mapped,committed_image,reserved_bytes"); CHECK(sample("baseline",0,0));
 for(unsigned n=0;n<100;n++){
 ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_0;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&fl,1,D3D11_SDK_VERSION,&d,nullptr,&c)));
 ID3D11Texture2D*t[2]{};
 if(textures){D3D11_TEXTURE2D_DESC desc{};desc.Width=1920;desc.Height=1080;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;for(auto &x:t)CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&x)));}
 c->Flush();Sleep(50);for(auto x:t)if(x)x->Release();c->ClearState();c->Flush();c->Release();ULONG refs=d->Release();CHECK(refs==0);CHECK(sample("cycle",n,refs));
 }
 for(unsigned sec=1;sec<=30;sec++){Sleep(1000);CHECK(sample("settle",sec,0));}return 0;
}
