#pragma once
// Linux provider adapter. The owner must keep this exact pool immutable for the
// complete session; this snapshot neither acquires pixel access nor pins a pool.
#include "av_memory_agreement.h"
#include "framelink_dmabuf.h"
#include <sys/stat.h>
#include <drm_fourcc.h>
#include <vector>
#include <cstring>
namespace av {
inline bool dmabufBinding(const flSessionConfig& cfg,const flChannelInfo& info,
                         const unsigned char deviceId[16],const flDmabuf* images,
                         std::array<unsigned char,32>& out,std::string& error){
 auto fail=[&](const char*t){error=t;return false;};
 if(!images||!deviceId||!info.poolSize||info.poolSize>FL_MAX_POOL_SIZE||info.width!=cfg.video.width||info.height!=cfg.video.height||info.format!=cfg.video.format)return fail("invalid DMA-BUF pool binding configuration");
 unsigned knownDevice=0;for(unsigned i=0;i<16;++i)knownDevice|=deviceId[i];if(!knownDevice)return fail("verified import device required");
 unsigned char session[FL_SESSION_WIRE_SIZE]{};if(flSessionEncode(&cfg,session,sizeof(session))!=FL_SESSION_OK)return fail("invalid binding session");
 auto*sum=g_checksum_new(G_CHECKSUM_SHA256);
 struct Cleanup{GChecksum*s;~Cleanup(){g_checksum_free(s);}} cleanup{sum};
 auto bytes=[&](const void*p,size_t n){g_checksum_update(sum,(const guchar*)p,n);};
 auto integer=[&](uint64_t v){unsigned char b[8];for(unsigned i=0;i<8;++i)b[i]=(unsigned char)(v>>(8*i));bytes(b,8);};
 static const char domain[]="FrameLink-Linux-immutable-DMA-BUF-pool-v1";
 bytes(domain,sizeof(domain));bytes(session,sizeof(session));bytes(deviceId,16);integer(info.poolSize);
 for(unsigned i=0;i<info.poolSize;++i){const auto&d=images[i];if(!d.planes||d.planes>4||d.modifier==DRM_FORMAT_MOD_INVALID)return fail("unknown DMA-BUF native layout");
  integer(i);integer(d.fourcc);integer(d.modifier);integer(d.planes);
  for(unsigned p=0;p<d.planes;++p){struct stat st{};if(d.fd[p]<0||fstat(d.fd[p],&st)||!d.stride[p])return fail("unavailable DMA-BUF allocation identity");
   // Kernel backing-object identity, NEVER the process-local descriptor number.
   integer(st.st_dev);integer(st.st_ino);integer(d.offset[p]);integer(d.stride[p]);
  }
 }
 std::array<unsigned char,32> result{};gsize n=result.size();g_checksum_get_digest(sum,result.data(),&n);out=result;error.clear();return true;
}
// verifiedDeviceId must identify the actual caller import/render device. The
// transport-only producer has no GPU device; this API does not infer one.
inline bool importedDmabufBinding(flChannel* channel,const flSessionConfig& cfg,
                                const unsigned char verifiedDeviceId[16],
                                std::array<unsigned char,32>& out,std::string& error){
 flChannelInfo info{};if(!verifiedDeviceId||flQuery(channel,&info)!=FL_OK||!info.poolSize||info.poolSize>FL_MAX_POOL_SIZE){error="invalid imported pool";return false;}
 std::vector<flBuffer> held;struct Cleanup{flChannel*c;std::vector<flBuffer>&b;~Cleanup(){for(auto&v:b)flCancelBuffer(c,&v);}} cleanup{channel,held};
 std::vector<flDmabuf> images(info.poolSize);std::vector<bool> seen(info.poolSize);unsigned char device[16]{};memcpy(device,verifiedDeviceId,16);
 for(unsigned i=0;i<info.poolSize;++i){flBuffer b{};if(flAcquireBuffer(channel,&b,0)!=FL_OK){error="pool must be idle before memory agreement";return false;}held.push_back(b);
  if(b.slot>=info.poolSize||seen[b.slot]){error="invalid imported pool slot";return false;}seen[b.slot]=true;
  flMemoryResourceInfo r{};r.version=FL_MEMORY_INFO_VERSION;
  if(flImageDmabuf(b.image,&images[b.slot])!=FL_OK||flImageMemoryInfo(channel,b.image,&r,sizeof(r))!=FL_OK){error="unknown imported device/layout";return false;}
  if((r.knownFields&FL_MEMORY_INFO_DEVICE)&&(r.deviceIdKind!=FL_MEMORY_DEVICE_DRM_NODE||memcmp(device,r.deviceId,16))){error="transport/import device mismatch";return false;}
 }
 flChannelInfo after{};if(flQuery(channel,&after)!=FL_OK||after.generation!=info.generation){error="pool changed during binding";return false;}
 return dmabufBinding(cfg,info,device,images.data(),out,error);
}
}
