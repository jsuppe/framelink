#include "../src/fl_shared_cpu_pool.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main() {
 flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;
 s.generation=7;s.videoTrackId=1;s.video={FL_FORMAT_BGRA8,16,16,60,1};
 s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;
 flSharedCpuDescriptor d[FL_MAX_POOL_SIZE]{};
 for(unsigned i=0;i<FL_MAX_POOL_SIZE;i++){
  d[i].version=1;d[i].resourceType=FL_MEMORY_SHARED_CPU;d[i].generation=7;d[i].allocationId=i+1;
  d[i].format=FL_FORMAT_BGRA8;d[i].width=d[i].height=16;d[i].planeCount=1;d[i].planeStride[0]=64;d[i].allocationBytes=1024;
 }
 flMemoryRequirements req[2]{};for(auto& r:req){CHECK(flMemoryRequirementsInit(&r)==FL_MEMORY_OK);r.allowedResourceTypes=FL_MEMORY_SHARED_CPU;r.allowedOperations=FL_MEMORY_COPY;}
 req[0].requiredUsage=FL_MEMORY_CPU_WRITE;req[1].requiredUsage=FL_MEMORY_CPU_READ;
 uint32_t ops[2]={FL_MEMORY_COPY,0};flMemoryDecision out[2]{};
 auto run=[&](size_t n=2){return fl::validateSharedCpuPool(&s,d,n,req,ops,out);};
 CHECK(run()==FL_MEMORY_OK);CHECK(run(16)==FL_MEMORY_OK);
 auto unchanged=[&](flMemoryResult expected,size_t n=2){memset(out,0xa5,sizeof(out));flMemoryDecision saved[2];memcpy(saved,out,sizeof(out));CHECK(run(n)==expected);CHECK(!memcmp(saved,out,sizeof(out)));};
 unchanged(FL_MEMORY_INVALID,0);unchanged(FL_MEMORY_INVALID,1);unchanged(FL_MEMORY_INVALID,17);
 s.generation++;unchanged(FL_MEMORY_INCOMPATIBLE);s.generation--;
 d[1].allocationId=1;unchanged(FL_MEMORY_INVALID);d[1].allocationId=2;
 d[1].format=FL_FORMAT_RGBA8;unchanged(FL_MEMORY_INCOMPATIBLE);d[1].format=FL_FORMAT_BGRA8;
 d[1].width=8;unchanged(FL_MEMORY_INCOMPATIBLE);d[1].width=16;
 d[1].allocationBytes=FL_SHARED_CPU_MAX_BYTES;unchanged(FL_MEMORY_INVALID);d[1].allocationBytes=1024;
 s.videoQueueFrames=2;unchanged(FL_MEMORY_INCOMPATIBLE);CHECK(run(4)==FL_MEMORY_OK);s.videoQueueFrames=1;
 s.version=9;unchanged(FL_MEMORY_VERSION_MISMATCH);s.version=2;
 ops[0]=FL_MEMORY_UPLOAD;unchanged(FL_MEMORY_INVALID);ops[0]=FL_MEMORY_COPY;
 req[1].allowedOperations=0;CHECK(run()==FL_MEMORY_INCOMPATIBLE);CHECK(out[1].forbiddenOperations==FL_MEMORY_COPY);
 req[1].allowedOperations=FL_MEMORY_COPY;
 for(unsigned endpoint=0;endpoint<2;endpoint++)for(uint32_t usage:{FL_MEMORY_GPU_SAMPLE,FL_MEMORY_GPU_RENDER,FL_MEMORY_DECODER_OUTPUT}){
  req[endpoint].requiredUsage|=usage;CHECK(run()==FL_MEMORY_INCOMPATIBLE);CHECK(out[endpoint].missingRequiredUsage==usage);req[endpoint].requiredUsage&=~usage;
 }
 req[1].requiredUsage|=FL_MEMORY_CPU_WRITE;CHECK(run()==FL_MEMORY_INCOMPATIBLE);req[1].requiredUsage=FL_MEMORY_CPU_READ;
 req[1].allowedResourceTypes=FL_MEMORY_DMABUF;CHECK(run()==FL_MEMORY_INCOMPATIBLE);req[1].allowedResourceTypes=FL_MEMORY_SHARED_CPU;
 ops[0]=0;CHECK(run()==FL_MEMORY_OK); // direct CPU writes/reads, no memcpy declared
 s.videoTrackId=0;s.video={};s.videoQueueFrames=0;s.audioTrackId=2;s.audio={FL_PCM_S16,48000,FL_LAYOUT_STEREO};s.audioQueueMs=10;unchanged(FL_MEMORY_INCOMPATIBLE);
 CHECK(fl::validateSharedCpuPool(nullptr,d,2,req,ops,out)==FL_MEMORY_INVALID);
 puts("shared CPU pool preflight: epoch/format/identity/bounds/queue, strict peer copy policy, CPU-only rights passed");
}
