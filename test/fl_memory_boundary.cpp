#include "framelink_memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 flMemoryRequirements r[3]{};for(auto&v:r)CHECK(flMemoryRequirementsInit(&v)==FL_MEMORY_OK);
 r[0].requiredUsage=FL_MEMORY_DECODER_OUTPUT;r[1].requiredUsage=FL_MEMORY_GPU_SAMPLE;r[2].requiredUsage=FL_MEMORY_CPU_READ;
 flMemoryPath p[3]{};for(auto&v:p){v.version=FL_MEMORY_VERSION;v.resourceType=FL_MEMORY_D3D11;}
 p[0].supportedUsage=FL_MEMORY_DECODER_OUTPUT;p[1].supportedUsage=FL_MEMORY_GPU_SAMPLE;p[2].supportedUsage=FL_MEMORY_CPU_READ;
 flMemoryDecision d[3]{};CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_OK);
 // Capabilities remain endpoint-specific: producer decode does not grant decode to a reader.
 r[1].requiredUsage|=FL_MEMORY_DECODER_OUTPUT;CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_INCOMPATIBLE&&d[1].missingRequiredUsage==FL_MEMORY_DECODER_OUTPUT);r[1].requiredUsage=FL_MEMORY_GPU_SAMPLE;
 p[0].operations=FL_MEMORY_COPY|FL_MEMORY_UPLOAD;r[0].allowedOperations=p[0].operations;
 CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_INCOMPATIBLE);
 CHECK(d[0].forbiddenOperations==0&&d[1].forbiddenOperations==p[0].operations&&d[2].forbiddenOperations==p[0].operations);
 for(auto&v:r)v.allowedOperations=FL_MEMORY_COPY|FL_MEMORY_UPLOAD;
 CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_OK);
 p[2].operations=FL_MEMORY_READBACK;CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_INCOMPATIBLE);
 for(auto&v:d)CHECK(v.forbiddenOperations==FL_MEMORY_READBACK);
 for(auto&v:r)v.allowedOperations|=FL_MEMORY_READBACK;
 r[1].preferredUsage=FL_MEMORY_GPU_RENDER;CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_OK&&d[1].unmetPreferredUsage==FL_MEMORY_GPU_RENDER);
 flMemoryRequirements combined{};CHECK(flMemoryRequirementsCombine(r,3,&combined)==FL_MEMORY_OK);
 CHECK(combined.requiredUsage==(FL_MEMORY_DECODER_OUTPUT|FL_MEMORY_GPU_SAMPLE|FL_MEMORY_CPU_READ));
 r[2].allowedOperations=0;CHECK(flMemoryRequirementsCombine(r,3,&combined)==FL_MEMORY_OK&&combined.allowedOperations==0);
 auto saved=combined;r[0].allowedResourceTypes=FL_MEMORY_D3D11;r[1].allowedResourceTypes=FL_MEMORY_AHB;
 CHECK(flMemoryRequirementsCombine(r,3,&combined)==FL_MEMORY_INCOMPATIBLE&&!memcmp(&saved,&combined,sizeof(saved)));
 r[1].allowedResourceTypes=FL_MEMORY_RESOURCE_ALL;
 std::memset(d,0xa5,sizeof(d));flMemoryDecision sentinel[3];std::memcpy(sentinel,d,sizeof(d));
 p[2].resourceType=FL_MEMORY_AHB;CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_INVALID&&!memcmp(d,sentinel,sizeof(d)));p[2].resourceType=FL_MEMORY_D3D11;
 r[2].version=999;CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_VERSION_MISMATCH&&!memcmp(d,sentinel,sizeof(d)));r[2].version=1;
 r[2].reserved[2]=1;CHECK(flMemoryEvaluateBoundary(r,p,3,d)==FL_MEMORY_INVALID&&!memcmp(d,sentinel,sizeof(d)));r[2].reserved[2]=0;
 CHECK(flMemoryEvaluateBoundary(r,p,0,d)==FL_MEMORY_INVALID);CHECK(flMemoryEvaluateBoundary(r,p,17,d)==FL_MEMORY_INVALID);
 CHECK(flMemoryRequirementsCombine(r,0,&combined)==FL_MEMORY_INVALID);CHECK(flMemoryRequirementsCombine(r,17,&combined)==FL_MEMORY_INVALID);
 std::array<flMemoryRequirements,16> many{};std::array<flMemoryPath,16> paths{};std::array<flMemoryDecision,16> decisions{};
 for(size_t i=0;i<16;++i){CHECK(flMemoryRequirementsInit(&many[i])==FL_MEMORY_OK);paths[i]={1,FL_MEMORY_DMABUF,FL_MEMORY_GPU_SAMPLE,0,{}};many[i].requiredUsage=FL_MEMORY_GPU_SAMPLE;}
 CHECK(flMemoryEvaluateBoundary(many.data(),paths.data(),16,decisions.data())==FL_MEMORY_OK);
 many[15].requiredUsage|=FL_MEMORY_CPU_WRITE;CHECK(flMemoryEvaluateBoundary(many.data(),paths.data(),16,decisions.data())==FL_MEMORY_INCOMPATIBLE&&decisions[15].missingRequiredUsage==FL_MEMORY_CPU_WRITE);
 puts("boundary agreement: endpoint usages, all-participant operation consent, conflicts and bounded negatives pass");
}
