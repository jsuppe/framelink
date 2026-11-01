#pragma once
#include "av_memory_agreement.h"
#include "framelink_d3d11.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
namespace av {
// Authority-issued ID for ONE canonical shared NT export. Broker must deliver
// duplicates of that exact export, bound to its authorized session/lease. A new
// CreateSharedHandle export is not interchangeable, even for the same pixels.
struct D3DPoolEntry {void* handle=nullptr;std::array<unsigned char,16> allocationId{};};
using CompareNtHandles=BOOL(WINAPI*)(HANDLE,HANDLE);
inline CompareNtHandles compareNtHandles(){return (CompareNtHandles)GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles");}
inline bool d3dPoolBinding(ID3D11Device*device,const flSessionConfig&cfg,const D3DPoolEntry*entries,unsigned count,std::array<unsigned char,32>&out,std::string&error){
 using Microsoft::WRL::ComPtr;auto fail=[&](const char*t){error=t;return false;};auto compare=compareNtHandles();
 if(!device||!entries||!count||count>FL_MAX_POOL_SIZE||!compare)return fail("canonical NT pool identity unavailable");
 unsigned char session[FL_SESSION_WIRE_SIZE]{};if(flSessionEncode(&cfg,session,sizeof(session))!=FL_SESSION_OK)return fail("invalid binding session");
 ComPtr<ID3D11Device1> dev1;ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC ad{};
 if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dev1)))||FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))||FAILED(dxgi->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&ad)))return fail("unknown D3D import adapter");
 auto*sum=g_checksum_new(G_CHECKSUM_SHA256);struct Cleanup{GChecksum*s;~Cleanup(){g_checksum_free(s);}} cleanup{sum};
 auto bytes=[&](const void*p,size_t n){g_checksum_update(sum,(const guchar*)p,n);};auto integer=[&](uint64_t v){unsigned char b[8];for(unsigned i=0;i<8;++i)b[i]=(unsigned char)(v>>(8*i));bytes(b,8);};
 static const char domain[]="FrameLink-canonical-NT-pool-v1";bytes(domain,sizeof(domain));bytes(session,sizeof(session));integer(ad.AdapterLuid.LowPart);integer(uint32_t(ad.AdapterLuid.HighPart));integer(count);
 auto format=cfg.video.format==FL_FORMAT_NV12?DXGI_FORMAT_NV12:cfg.video.format==FL_FORMAT_RGBA8?DXGI_FORMAT_R8G8B8A8_UNORM:cfg.video.format==FL_FORMAT_RGB10A2?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
 for(unsigned i=0;i<count;++i){auto&entry=entries[i];unsigned nonzero=0;for(auto b:entry.allocationId)nonzero|=b;
  if(!entry.handle||entry.handle==INVALID_HANDLE_VALUE||!nonzero)return fail("missing canonical handle/allocator identity");
  for(unsigned j=0;j<i;++j)if(entry.allocationId==entries[j].allocationId||compare(entry.handle,entries[j].handle))return fail("aliased canonical pool entries");
  ComPtr<ID3D11Texture2D> texture;if(FAILED(dev1->OpenSharedResource1(entry.handle,IID_PPV_ARGS(&texture))))return fail("canonical handle import rejected");
  D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
  if(d.Width!=cfg.video.width||d.Height!=cfg.video.height||d.Format!=format||d.MipLevels!=1||d.ArraySize!=1||d.SampleDesc.Count!=1||!(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED_NTHANDLE))return fail("canonical texture layout mismatch");
  integer(i);bytes(entry.allocationId.data(),16);integer(d.Width);integer(d.Height);integer(d.Format);integer(d.MipLevels);integer(d.ArraySize);integer(d.SampleDesc.Count);integer(d.SampleDesc.Quality);integer(d.Usage);integer(d.BindFlags);integer(d.CPUAccessFlags);integer(d.MiscFlags);
 }
 std::array<unsigned char,32> result{};gsize n=result.size();g_checksum_get_digest(sum,result.data(),&n);out=result;error.clear();return true;
}
inline bool importedD3DBinding(flChannel*channel,const flSessionConfig&cfg,const D3DPoolEntry*entries,unsigned count,std::array<unsigned char,32>&out,std::string&error){
 auto compare=compareNtHandles();flChannelInfo info{};if(!entries||!compare||flQuery(channel,&info)!=FL_OK||count!=info.poolSize||!count||count>FL_MAX_POOL_SIZE){error="invalid canonical imported pool";return false;}
 std::vector<flBuffer> held;struct Cleanup{flChannel*c;std::vector<flBuffer>&v;~Cleanup(){for(auto&b:v)flCancelBuffer(c,&b);}} cleanup{channel,held};std::vector<bool> seen(count);
 for(unsigned i=0;i<count;++i){flBuffer b{};if(flAcquireBuffer(channel,&b,0)!=FL_OK){error="D3D pool must be idle before agreement";return false;}held.push_back(b);
  if(b.slot>=count||seen[b.slot]||!compare(flImageSharedHandle(b.image),entries[b.slot].handle)){error="import does not match broker canonical NT export";return false;}seen[b.slot]=true;
 }
 flChannelInfo after{};if(flQuery(channel,&after)!=FL_OK||after.generation!=info.generation){error="D3D pool changed during binding";return false;}
 return d3dPoolBinding(flChannelD3D11Device(channel),cfg,entries,count,out,error);
}
}
