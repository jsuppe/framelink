#include "av_memory_d3d11.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Microsoft::WRL::ComPtr;
static void run(bool nv12){flSessionConfig cfg{};cfg.version=1;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.video={uint32_t(nv12?FL_FORMAT_NV12:FL_FORMAT_RGBA8),64,32,30,1};cfg.audio={FL_PCM_S16,48000,FL_LAYOUT_STEREO};cfg.epochNs=1000000000;cfg.videoQueueFrames=2;cfg.audioQueueMs=40;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 ComPtr<ID3D11Device> device;CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC ad{};CHECK(SUCCEEDED(device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&ad)));
 D3D11_TEXTURE2D_DESC d{};d.Width=64;d.Height=32;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=nv12?DXGI_FORMAT_NV12:DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ComPtr<ID3D11Texture2D> textures[3];av::D3DPoolEntry entries[3];std::string e;for(unsigned i=0;i<3;++i){CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&textures[i])));ComPtr<IDXGIResource1> r;CHECK(SUCCEEDED(textures[i].As(&r))&&SUCCEEDED(r->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&entries[i].handle)));CHECK(av::secureRandom(entries[i].allocationId.data(),16,e));}
 void* handles[]={entries[0].handle,entries[1].handle};flChannel*c=nullptr,*p=nullptr;auto name="canonical-binding-"+std::to_string(GetCurrentProcessId())+std::to_string(nv12);CHECK(flCreateChannelFromD3D11Handles(name.c_str(),64,32,flFormat(cfg.video.format),2,0,(const uint8_t*)&ad.AdapterLuid,handles,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 std::array<unsigned char,32> owner{},imported{},other{};CHECK(av::d3dPoolBinding(device.Get(),cfg,entries,2,owner,e));CHECK(av::importedD3DBinding(p,cfg,entries,2,imported,e)&&owner==imported);
 av::D3DPoolEntry duplicates[]={entries[0],entries[1]};for(auto&entry:duplicates){void* duplicate=nullptr;CHECK(DuplicateHandle(GetCurrentProcess(),entry.handle,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS));entry.handle=duplicate;}
 CHECK(av::importedD3DBinding(p,cfg,duplicates,2,other,e)&&other==owner);for(auto&entry:duplicates)CloseHandle(entry.handle);
 av::D3DPoolEntry wrong[]={entries[0],entries[1]};wrong[1].handle=entries[2].handle;other.fill(0x55);auto unchanged=other;CHECK(!av::importedD3DBinding(p,cfg,wrong,2,other,e)&&other==unchanged);
 av::D3DPoolEntry alias[]={entries[0],entries[0]};CHECK(!av::d3dPoolBinding(device.Get(),cfg,alias,2,other,e)&&other==unchanged);
 flBuffer held{};CHECK(flAcquireBuffer(p,&held,0)==FL_OK);CHECK(!av::importedD3DBinding(p,cfg,entries,2,other,e)&&other==unchanged);CHECK(flCancelBuffer(p,&held)==FL_OK);CHECK(av::importedD3DBinding(p,cfg,entries,2,other,e)&&other==owner);
 flClose(p);flClose(c);for(auto&entry:entries)CloseHandle(entry.handle);printf("%s canonical NT pool: exact imported-export proof, substitution/alias and partial-reservation cleanup pass\n",nv12?"NV12":"RGBA");}
int main(){run(false);run(true);}
