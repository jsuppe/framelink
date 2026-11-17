#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <d3d10.h>
#include <gst/gst.h>
#include <gst/d3d11/gstd3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d:%s\n",__LINE__,#x);exit(1);}}while(0)
static void sample(unsigned cycle,ULONG refs){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;CHECK(K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof p));DWORD h=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&h));printf("%u,%lld,%llu,%lu,%lu,%lu\n",cycle,(long long)g_get_monotonic_time(),(unsigned long long)p.PrivateUsage,p.PageFaultCount,h,refs);fflush(stdout);}
int main(int argc,char**argv){gst_init(&argc,&argv);CHECK(argc==3);bool wrapped=!strcmp(argv[1],"wrapped");CHECK(wrapped||!strcmp(argv[1],"raw"));unsigned count=atoi(argv[2]);fprintf(stderr,"mode=%s cycles=%u twoBGRA1080ptextures 300frames60fps/cycle\n",argv[1],count);puts("cycle,monotonic_us,private_bytes,page_faults,handles,device_release_refs");
for(unsigned cycle=0;cycle<count;cycle++){
 ID3D11Device*dev=nullptr;ID3D11DeviceContext*ctx=nullptr;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_1;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&level,1,D3D11_SDK_VERSION,&dev,nullptr,&ctx)));
 ID3D10Multithread*mt=nullptr;CHECK(SUCCEEDED(dev->QueryInterface(__uuidof(ID3D10Multithread),(void**)&mt)));mt->SetMultithreadProtected(TRUE);mt->Release();
 auto*gstdev=wrapped?gst_d3d11_device_new_wrapped(dev):nullptr;if(wrapped)CHECK(gstdev);
 D3D11_TEXTURE2D_DESC d{};d.Width=1920;d.Height=1080;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ID3D11Texture2D*tex[2]{};gsize sizes[2]{};for(unsigned i=0;i<2;i++){CHECK(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&tex[i])));}
 auto start=g_get_monotonic_time();
 for(unsigned frame=0;frame<300;frame++){
  if(wrapped){unsigned slot=frame%2;auto*m=gst_d3d11_allocator_alloc_wrapped(nullptr,gstdev,tex[slot],sizes[slot],nullptr,nullptr);CHECK(m);if(!sizes[slot]){guint pitch=0;CHECK(gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(m),&pitch)&&pitch>=1920*4);sizes[slot]=m->size;}CHECK(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(m))==tex[slot]);gst_memory_unref(m);}
  ctx->Flush();auto wait=start+(gint64(frame)+1)*G_USEC_PER_SEC/60-g_get_monotonic_time();if(wait>0)g_usleep(wait);
 }
 for(auto*t:tex)t->Release();if(gstdev)gst_object_unref(gstdev);ctx->Flush();ctx->Release();ULONG refs=dev->Release();sample(cycle,refs);
}
gst_deinit();return 0;}
