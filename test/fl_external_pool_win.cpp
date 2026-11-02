#include "framelink.h"
#include "framelink_memory.h"
#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int main(int argc,char** argv) {
 flFormat format=argc>1?flFormat(std::strtoul(argv[1],nullptr,10)):FL_FORMAT_NV12;
 const DXGI_FORMAT dx= format==FL_FORMAT_NV12?DXGI_FORMAT_NV12:format==FL_FORMAT_RGBA8?DXGI_FORMAT_R8G8B8A8_UNORM:format==FL_FORMAT_RGB10A2?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
 ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC ad{};
 CHECK(SUCCEEDED(device.As(&dxgi)));CHECK(SUCCEEDED(dxgi->GetAdapter(&adapter)));CHECK(SUCCEEDED(adapter->GetDesc(&ad)));
 D3D11_TEXTURE2D_DESC d{};d.Width=64;d.Height=32;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;
 d.Format=dx;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
 d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ComPtr<ID3D11Texture2D> original[2];void* handles[2]{};
 for(int n=0;n<2;n++) {
  CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&original[n])));ComPtr<IDXGIResource1> resource;
  CHECK(SUCCEEDED(original[n].As(&resource)));CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handles[n])));
 }
 std::string name="external-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 flChannel *c=nullptr,*p=nullptr;auto luid=reinterpret_cast<const uint8_t*>(&ad.AdapterLuid);
 void* alias=nullptr;CHECK(DuplicateHandle(GetCurrentProcess(),handles[0],GetCurrentProcess(),&alias,0,FALSE,DUPLICATE_SAME_ACCESS));
 void* aliases[]={handles[0],alias};
 CHECK(flCreateChannelFromD3D11Handles(name.c_str(),64,32,format,2,FL_MAP_CPU,luid,aliases,&c)==FL_INVALID);CHECK(!c);CloseHandle(alias);
 CHECK(flCreateChannelFromD3D11Handles(name.c_str(),128,32,format,2,FL_MAP_CPU,luid,handles,&c)==FL_FORMAT_UNSUPPORTED);CHECK(!c);
 CHECK(flCreateChannelFromD3D11Handles(name.c_str(),64,32,format,2,FL_MAP_CPU,luid,handles,&c)==FL_OK);
 for(auto handle:handles)CloseHandle(handle); // FrameLink must own independent native refs.
 CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 flChannelInfo before{},after{};CHECK(flQuery(p,&before)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,format)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);
 CHECK(after.width==64 && after.generation==before.generation); // External allocation immutable.
 for(unsigned frame=0;frame<3;frame++) {
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);flMappedImage map{};
  CHECK(flImageMapPlanes(b.image,&map)==FL_OK);
  for(unsigned plane=0;plane<map.planes;plane++)for(unsigned row=0;row<map.plane[plane].rows;row++)
   std::memset(static_cast<char*>(map.plane[plane].pixels)+row*map.plane[plane].stride,37+frame+plane,map.plane[plane].rowBytes);
  flImageUnmap(b.image);CHECK(flSubmit(p,&b,123+frame)==FL_OK);
  flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);CHECK(f.ptsNs==123+frame);
  flMemoryResourceInfo info{};info.version=FL_MEMORY_INFO_VERSION;CHECK(flImageMemoryInfo(c,f.image,&info,sizeof(info))==FL_OK);
  CHECK(info.interopCapabilities & FL_MEMORY_INTEROP_EXTERNAL_POOL);
  CHECK(flImageMapPlanes(f.image,&map)==FL_OK);
  for(unsigned plane=0;plane<map.planes;plane++)for(unsigned row=0;row<map.plane[plane].rows;row++)for(unsigned x=0;x<map.plane[plane].rowBytes;x++)
   CHECK(static_cast<unsigned char*>(map.plane[plane].pixels)[row*map.plane[plane].stride+x]==37+frame+plane);
  flImageUnmap(f.image);
  if(frame==0) {
   // Read the ORIGINAL allocation through its original device, not a FrameLink
   // staging texture. This verification readback is not a transport operation.
   D3D11_TEXTURE2D_DESC stage=d;stage.MiscFlags=stage.BindFlags=0;stage.Usage=D3D11_USAGE_STAGING;stage.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
   ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&stage,nullptr,&staging)));
   ctx->CopyResource(staging.Get(),original[b.slot].Get());D3D11_MAPPED_SUBRESOURCE view{};
   CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&view)));
   unsigned rows=format==FL_FORMAT_NV12?48:32,bytes=format==FL_FORMAT_NV12?64:256;
   for(unsigned row=0;row<rows;row++)for(unsigned x=0;x<bytes;x++)
    CHECK(static_cast<unsigned char*>(view.pData)[row*view.RowPitch+x]==(row>=32?38:37));
   ctx->Unmap(staging.Get(),0);
   original[0].Reset();original[1].Reset(); // Subsequent frames survive original owner dropping refs.
  }
  CHECK(flRelease(c,&f)==FL_OK);
 }
 flClose(p);flClose(c);
 std::printf("external D3D format=%u: alias/geometry rejection, immutable ring, original-allocation pixels and independent lifetime passed\n",unsigned(format));
}
