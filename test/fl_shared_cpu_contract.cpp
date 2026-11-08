#include "framelink_shared_cpu.h"
#include "framelink_session.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static flSharedCpuDescriptor descriptor(bool nv12=false){
 flSharedCpuDescriptor d{};d.version=FL_SHARED_CPU_VERSION;d.resourceType=FL_MEMORY_SHARED_CPU;d.format=nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8;
 d.width=1920;d.height=1080;d.planeCount=nv12?2:1;d.generation=0x0102030405060708ULL;d.allocationId=0x1122334455667788ULL;
 d.planeOffset[0]=64;d.planeStride[0]=nv12?2048:7680;
 d.allocationBytes=d.planeOffset[0]+d.planeStride[0]*d.height;
 if(nv12){d.planeOffset[1]=d.allocationBytes;d.planeStride[1]=2048;d.allocationBytes+=d.planeStride[1]*(d.height/2);}
 return d;
}
static void invalid(const flSharedCpuDescriptor&d,flMemoryResult expected=FL_MEMORY_INVALID){
 CHECK(flSharedCpuDescriptorValidate(&d)==expected);std::array<unsigned char,128> bytes;bytes.fill(0xa5);auto before=bytes;
 CHECK(flSharedCpuDescriptorEncode(&d,bytes.data(),bytes.size())==expected);CHECK(bytes==before);
}
int main(){
 static_assert(FL_VERSION==6&&FL_SESSION_VERSION==1&&FL_SESSION_WIRE_SIZE==160,"legacy protocols unchanged");
 static_assert(sizeof(flSharedCpuDescriptor)==128,"public descriptor ABI");
 for(auto format:{FL_FORMAT_BGRA8,FL_FORMAT_RGBA8,FL_FORMAT_RGB10A2,FL_FORMAT_NV12}){
  auto d=descriptor(format==FL_FORMAT_NV12);d.format=format;CHECK(flSharedCpuDescriptorValidate(&d)==FL_MEMORY_OK);
  unsigned char wire[128];CHECK(flSharedCpuDescriptorEncode(&d,wire,sizeof(wire))==FL_MEMORY_OK);
  CHECK(!memcmp(wire,"FCS1",4)&&wire[4]==1&&wire[8]==1);
  for(unsigned i=0;i<8;i++)CHECK(wire[32+i]==8-i); // fixed LE generation offset
  CHECK(wire[40]==0x88&&wire[47]==0x11);for(unsigned i=56;i<64;i++)CHECK(!wire[i]);
  flSharedCpuDescriptor decoded{};CHECK(flSharedCpuDescriptorDecode(wire,128,&decoded)==FL_MEMORY_OK);CHECK(!memcmp(&d,&decoded,sizeof(d)));
  auto alias=d;CHECK(flSharedCpuDescriptorEncode(&alias,&alias,128)==FL_MEMORY_OK);CHECK(flSharedCpuDescriptorDecode(&alias,128,&alias)==FL_MEMORY_OK);CHECK(!memcmp(&alias,&d,sizeof(d)));
  for(size_t size:{size_t(0),size_t(127),size_t(129)}){auto saved=decoded;CHECK(flSharedCpuDescriptorDecode(wire,size,&decoded)==FL_MEMORY_INVALID);CHECK(!memcmp(&saved,&decoded,sizeof(saved)));}
  for(unsigned index:{0u,8u,28u,56u,63u}){unsigned char bad[128];memcpy(bad,wire,128);bad[index]^=0x80;auto saved=decoded;CHECK(flSharedCpuDescriptorDecode(bad,128,&decoded)==FL_MEMORY_INVALID);CHECK(!memcmp(&saved,&decoded,sizeof(saved)));}
  {auto saved=decoded;wire[4]=2;CHECK(flSharedCpuDescriptorDecode(wire,128,&decoded)==FL_MEMORY_VERSION_MISMATCH);CHECK(!memcmp(&saved,&decoded,sizeof(saved)));}
 }
 auto d=descriptor();CHECK(flSharedCpuDescriptorValidate(nullptr)==FL_MEMORY_INVALID);
 CHECK(flSharedCpuDescriptorEncode(&d,nullptr,128)==FL_MEMORY_INVALID);CHECK(flSharedCpuDescriptorDecode(nullptr,128,&d)==FL_MEMORY_INVALID);
 unsigned char wire[128]{};CHECK(flSharedCpuDescriptorDecode(wire,128,nullptr)==FL_MEMORY_INVALID);
#define BAD(field,value) do{auto bad=d;bad.field=value;invalid(bad);}while(0)
 for(uint32_t version:{0u,2u,UINT32_MAX}){auto bad=d;bad.version=version;invalid(bad,FL_MEMORY_VERSION_MISMATCH);}
 BAD(resourceType,FL_MEMORY_DMABUF);BAD(resourceType,FL_MEMORY_SHARED_CPU|FL_MEMORY_AHB);
 BAD(format,0);BAD(format,UINT32_MAX);BAD(width,0);BAD(height,0);BAD(width,8193);BAD(height,8193);
 BAD(planeCount,0);BAD(planeCount,2);BAD(planeCount,5);BAD(flags,1);BAD(reserved32,1);BAD(reserved64,1);
 BAD(generation,0);BAD(allocationId,0);BAD(allocationBytes,0);BAD(allocationBytes,FL_SHARED_CPU_MAX_BYTES+1);
 BAD(planeOffset[0],1);BAD(planeOffset[0],UINT64_MAX);BAD(planeStride[0],0);BAD(planeStride[0],7676);BAD(planeStride[0],7681);BAD(planeStride[0],UINT64_MAX-3);
 BAD(planeOffset[1],64);BAD(planeStride[3],4);BAD(allocationBytes,d.allocationBytes-1);
 d=descriptor(true);BAD(width,1919);BAD(height,1079);BAD(planeCount,1);BAD(planeOffset[1],d.planeOffset[1]-2);BAD(planeStride[1],2049);BAD(planeStride[1],UINT64_MAX-1);BAD(planeOffset[2],4);
 // A descriptor alone cannot weaken either participant's memory policy.
 flMemoryRequirements req[2]{};flMemoryPath paths[2]{};flMemoryDecision decisions[2]{};
 for(unsigned i=0;i<2;i++){CHECK(flMemoryRequirementsInit(&req[i])==FL_MEMORY_OK);req[i].allowedResourceTypes=FL_MEMORY_SHARED_CPU;req[i].allowedOperations=FL_MEMORY_COPY;req[i].requiredUsage=i?FL_MEMORY_CPU_READ:FL_MEMORY_CPU_WRITE;paths[i]={FL_MEMORY_VERSION,FL_MEMORY_SHARED_CPU,req[i].requiredUsage,FL_MEMORY_COPY,{}};}
 CHECK(flMemoryEvaluateBoundary(req,paths,2,decisions)==FL_MEMORY_OK);
 for(unsigned i=0;i<2;i++){auto saved=req[i];req[i].allowedOperations=0;CHECK(flMemoryEvaluateBoundary(req,paths,2,decisions)==FL_MEMORY_INCOMPATIBLE);CHECK(decisions[i].forbiddenOperations==FL_MEMORY_COPY);req[i]=saved;req[i].requiredUsage|=FL_MEMORY_GPU_SAMPLE;CHECK(flMemoryEvaluateBoundary(req,paths,2,decisions)==FL_MEMORY_INCOMPATIBLE);CHECK(decisions[i].missingRequiredUsage&FL_MEMORY_GPU_SAMPLE);req[i]=saved;}
 req[1].allowedResourceTypes=FL_MEMORY_DMABUF;CHECK(flMemoryEvaluateBoundary(req,paths,2,decisions)==FL_MEMORY_INCOMPATIBLE);
 // Deterministic malformed-wire mutations: every rejection preserves output;
 // any accepted mutation must survive an independent validate + re-encode.
 d=descriptor(true);CHECK(flSharedCpuDescriptorEncode(&d,wire,128)==FL_MEMORY_OK);
 for(unsigned i=0;i<128;i++)for(unsigned bit=0;bit<8;bit++){
  unsigned char bad[128];memcpy(bad,wire,128);bad[i]^=1u<<bit;auto out=descriptor();const auto saved=out;
  auto result=flSharedCpuDescriptorDecode(bad,128,&out);
  if(result!=FL_MEMORY_OK)CHECK(!memcmp(&out,&saved,sizeof(out)));
  else{CHECK(flSharedCpuDescriptorValidate(&out)==FL_MEMORY_OK);unsigned char again[128];CHECK(flSharedCpuDescriptorEncode(&out,again,128)==FL_MEMORY_OK);CHECK(!memcmp(bad,again,128));}
 }
 puts("shared-CPU contract: formats, fixed wire, overflow/overlap, aliasing, 1024 wire mutations and policy negatives passed");
}
