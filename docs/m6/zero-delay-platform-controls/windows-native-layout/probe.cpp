#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <gst/gst.h>
#include <gst/d3d11/gstd3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void sample(unsigned frame){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;CHECK(K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof p));DWORD handles=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles));printf("%u,%lld,%llu,%lu,%lu\n",frame,(long long)g_get_monotonic_time(),(unsigned long long)p.PrivateUsage,p.PageFaultCount,handles);fflush(stdout);}
int main(int argc,char**argv){gst_init(&argc,&argv);CHECK(argc>=3);bool cached=!strcmp(argv[1],"cached");CHECK(cached||!strcmp(argv[1],"probe"));unsigned count=unsigned(atoi(argv[2]));unsigned fps=argc>3?unsigned(atoi(argv[3])):0;bool flush=argc>4&&atoi(argv[4]);
 auto*device=gst_d3d11_device_new(0,D3D11_CREATE_DEVICE_BGRA_SUPPORT);CHECK(device);auto*d=gst_d3d11_device_get_device_handle(device);CHECK(d);ID3D11DeviceContext*ctx=nullptr;d->GetImmediateContext(&ctx);CHECK(ctx);
 D3D11_TEXTURE2D_DESC desc{};desc.Width=3840;desc.Height=2160;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_NV12;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ID3D11Texture2D*tex=nullptr;CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&tex)));
 auto*initial=gst_d3d11_allocator_alloc_wrapped(nullptr,device,tex,0,nullptr,nullptr);CHECK(initial);guint pitch=0;CHECK(gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(initial),&pitch)&&pitch>=3840);gsize size=initial->size;gst_memory_unref(initial);
 fprintf(stderr,"version=%s mode=%s frames=%u fps=%u pitch=%u size=%llu\n",gst_version_string(),argv[1],count,fps,pitch,(unsigned long long)size);
 puts("frame,monotonic_us,private_bytes,page_faults,handles");sample(0);auto start=g_get_monotonic_time();
 for(unsigned i=0;i<count;i++){
  auto*mem=gst_d3d11_allocator_alloc_wrapped(nullptr,device,tex,cached?size:0,nullptr,nullptr);CHECK(mem&&mem->size==size);CHECK(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem))==tex);
  if(!cached){guint stride=0;CHECK(gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(mem),&stride)&&stride==pitch);}
  gst_memory_unref(mem);if(flush)ctx->Flush();
  if(fps){auto wait=start+(gint64(i)+1)*G_USEC_PER_SEC/fps-g_get_monotonic_time();if(wait>0)g_usleep(wait);}
  if((i+1)%1000==0)sample(i+1);
 }
 sample(count);
 // Edge case: explicit CPU map of an independently sized wrapper must discover
 // the same pitch and expose the full storage, not an assumed logical stride.
 auto*edge=gst_d3d11_allocator_alloc_wrapped(nullptr,device,tex,size,nullptr,nullptr);CHECK(edge);
 GST_MINI_OBJECT_FLAG_SET(edge,GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
 GstMapInfo map{};CHECK(gst_memory_map(edge,&map,GST_MAP_READ));guint mappedPitch=0;CHECK(gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(edge),&mappedPitch)&&mappedPitch==pitch&&map.size==size);gst_memory_unmap(edge,&map);gst_memory_unref(edge);
 tex->Release();ctx->Flush();ctx->Release();gst_object_unref(device);sample(count+1);gst_deinit();return 0;
}
