#pragma once
#include "av_memory_agreement.h"
#include "framelink_ahb.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <vector>
namespace av {
// Exact AHB allocation binding, API31+. Keep the owner pool immutable until
// session end. This does not invent an opaque GPU device UUID or plane layout;
// endpoint GPU import/completion capability must be independently qualified.
inline bool ahbBinding(const flSessionConfig&cfg,AHardwareBuffer*const*images,
                       unsigned count,std::array<unsigned char,32>&out,std::string&error){
 auto fail=[&](const char*t){error=t;return false;};
 if(!images||!count||count>FL_MAX_POOL_SIZE||(cfg.video.format!=FL_FORMAT_RGBA8&&cfg.video.format!=FL_FORMAT_RGB10A2))return fail("exact native RGB AHB pool required");
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);auto getId=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");if(!getId)return fail("AHB allocation identity requires API31+");
 unsigned char session[FL_SESSION_WIRE_SIZE]{};if(flSessionEncode(&cfg,session,sizeof(session))!=FL_SESSION_OK)return fail("invalid binding session");
 auto*sum=g_checksum_new(G_CHECKSUM_SHA256);struct Cleanup{GChecksum*s;~Cleanup(){g_checksum_free(s);}} cleanup{sum};
 auto bytes=[&](const void*p,size_t n){g_checksum_update(sum,(const guchar*)p,n);};auto integer=[&](uint64_t v){unsigned char b[8];for(unsigned i=0;i<8;++i)b[i]=(unsigned char)(v>>(8*i));bytes(b,8);};
 static const char domain[]="FrameLink-Android-immutable-native-RGB-AHB-pool-v1";bytes(domain,sizeof(domain));bytes(session,sizeof(session));integer(count);
 std::vector<uint64_t> ids;
 for(unsigned i=0;i<count;++i){if(!images[i])return fail("missing AHB");AHardwareBuffer_Desc d{};AHardwareBuffer_describe(images[i],&d);uint64_t id=0;
  auto format=cfg.video.format==FL_FORMAT_RGBA8?AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
  if(d.width!=cfg.video.width||d.height!=cfg.video.height||d.layers!=1||d.format!=uint32_t(format)||getId(images[i],&id)||!id)return fail("AHB identity/layout mismatch");
  for(auto previous:ids)if(previous==id)return fail("aliased AHB pool slots");ids.push_back(id);
  integer(i);integer(id);integer(d.width);integer(d.height);integer(d.layers);integer(d.format);integer(d.usage);integer(d.stride);
 }
 std::array<unsigned char,32> result{};gsize n=result.size();g_checksum_get_digest(sum,result.data(),&n);out=result;error.clear();return true;
}
inline bool importedAhbBinding(flChannel*channel,const flSessionConfig&cfg,
                               std::array<unsigned char,32>&out,std::string&error){
 flChannelInfo info{};if(flQuery(channel,&info)!=FL_OK||!info.poolSize||info.poolSize>FL_MAX_POOL_SIZE){error="invalid imported AHB pool";return false;}
 std::vector<flBuffer> held;struct Cleanup{flChannel*c;std::vector<flBuffer>&v;~Cleanup(){for(auto&b:v)flCancelBuffer(c,&b);}} cleanup{channel,held};std::vector<AHardwareBuffer*> images(info.poolSize);
 for(unsigned i=0;i<info.poolSize;++i){flBuffer b{};if(flAcquireBuffer(channel,&b,0)!=FL_OK){error="AHB pool must be idle before agreement";return false;}held.push_back(b);if(b.slot>=images.size()||images[b.slot]){error="invalid imported AHB slot";return false;}images[b.slot]=flImageAHardwareBuffer(b.image);}
 flChannelInfo after{};if(flQuery(channel,&after)!=FL_OK||after.generation!=info.generation){error="AHB pool changed during binding";return false;}
 return ahbBinding(cfg,images.data(),images.size(),out,error);
}
}
