#include "../src/fl_shared_cpu_imported_pool_win.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static DWORD fdCount(){DWORD n=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&n));return n;}
struct Offer {
 flSharedCpuDescriptor d[2]{};fl::SharedCpuAllocationWin owner[2];HANDLE fds[2]={nullptr,nullptr};
 explicit Offer(uint64_t epoch){for(unsigned i=0;i<2;i++){
  auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.generation=epoch;v.allocationId=i+1;
  v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.planeStride[0]=64;v.allocationBytes=4096;
  CHECK(fl::SharedCpuAllocationWin::create(v,owner[i])==FL_OK);CHECK(owner[i].exportHandle(true,GetCurrentProcess(),fds[i])==FL_OK);
 }}
 ~Offer(){for(auto fd:fds)if(fd)CloseHandle(fd);}
};
int main(){
 {Offer warm(1);bool same=false;CHECK(warm.owner[0].sameStorage(warm.owner[1].identity(),same)==FL_OK&&!same);}
 const auto baseline=fdCount();
 {
 flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=1;s.videoTrackId=1;
 s.video={FL_FORMAT_BGRA8,16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;
 flMemoryRequirements req[2]{};for(auto&r:req){CHECK(flMemoryRequirementsInit(&r)==FL_MEMORY_OK);r.allowedResourceTypes=FL_MEMORY_SHARED_CPU;r.allowedOperations=FL_MEMORY_COPY;}
 req[0].requiredUsage=FL_MEMORY_CPU_WRITE;req[1].requiredUsage=FL_MEMORY_CPU_READ;uint32_t ops[2]={FL_MEMORY_COPY,0};
 fl::SharedCpuImportedPoolWin pool;
 auto adopt=[&](Offer&o,bool producer=false){return fl::SharedCpuImportedPoolWin::adopt(s,o.d,o.fds,2,req,ops,producer,pool);};
 Offer first(1);CHECK(adopt(first)==FL_OK);CHECK(pool.size()==2&&pool.generation()==1);
 fl::SharedCpuAllocationWin::Mapping held;CHECK(pool.map(0,held)==FL_OK);
 fl::SharedCpuAllocationWin::Mapping writer;CHECK(first.owner[0].map(true,writer)==FL_OK);
 *static_cast<unsigned char*>(writer.view().plane[0].pixels)=73;
 CHECK(*static_cast<unsigned char*>(held.view().plane[0].pixels)==73);
 CHECK(pool.map(2,held)==FL_INVALID);CHECK(bool(held));
 CHECK(adopt(first)==FL_INVALID); // stale epoch
 Offer second(2);s.generation=2;
 HANDLE save=second.fds[1];second.fds[1]=nullptr;const auto before=fdCount();
 for(unsigned i=0;i<100;i++)CHECK(adopt(second)==FL_INVALID);
 CHECK(fdCount()==before);CHECK(pool.generation()==1);second.fds[1]=save;
 second.fds[1]=second.fds[0];CHECK(adopt(second)==FL_INVALID);second.fds[1]=save; // different IDs, same object
 CHECK(adopt(second)==FL_OK);CHECK(pool.generation()==2);
 // New logical IDs/epoch do not authorize reusing held old storage.
 first.d[0].generation=first.d[1].generation=3;s.generation=3;CHECK(adopt(first)==FL_INVALID);
 Offer third(3);CHECK(adopt(third)==FL_OK);
 first.d[0].generation=first.d[1].generation=4;s.generation=4;CHECK(adopt(first)==FL_INVALID); // two generations retired
 CHECK(*static_cast<unsigned char*>(held.view().plane[0].pixels)==73);
 // Storage with no importer/mapping owners may eventually be reused.
 held.reset();writer.reset();Offer fourth(4);CHECK(adopt(fourth)==FL_OK);
 first.d[0].generation=first.d[1].generation=5;s.generation=5;CHECK(adopt(first)==FL_OK);
 // An RO handle cannot become a producer's writable slot; failed import is atomic.
 fl::SharedCpuImportedPoolWin producer;HANDLE ro=nullptr;CHECK(first.owner[1].exportHandle(false,GetCurrentProcess(),ro)==FL_OK);
 HANDLE rw=first.fds[1];first.fds[1]=ro;
 CHECK(fl::SharedCpuImportedPoolWin::adopt(s,first.d,first.fds,2,req,ops,true,producer)==FL_ACCESS_DENIED);
 CHECK(producer.size()==0);first.fds[1]=rw;CloseHandle(ro);
 CHECK(fl::SharedCpuImportedPoolWin::adopt(s,first.d,first.fds,2,req,ops,true,producer)==FL_OK);
 fl::SharedCpuImportedPoolWin moved=std::move(producer);CHECK(producer.size()==0);producer=std::move(moved);CHECK(moved.size()==0);
 CHECK(producer.map(1,writer)==FL_OK);*static_cast<unsigned char*>(writer.view().plane[0].pixels)=91;
 fl::SharedCpuAllocationWin::Mapping reader;CHECK(pool.map(1,reader)==FL_OK);CHECK(*static_cast<unsigned char*>(reader.view().plane[0].pixels)==91);
 // Retired generation tracking is bounded and recovers after holders release.
 fl::SharedCpuImportedPoolWin bounded;
 std::vector<fl::SharedCpuAllocationWin::Mapping> pins;
 for(unsigned epoch=1;epoch<=128;++epoch){
  Offer o(epoch);s.generation=epoch;
  CHECK(fl::SharedCpuImportedPoolWin::adopt(s,o.d,o.fds,2,req,ops,false,bounded)==FL_OK);
  for(unsigned slot=0;slot<2;++slot){fl::SharedCpuAllocationWin::Mapping pin;CHECK(bounded.map(slot,pin)==FL_OK);pins.push_back(pin);}
 }
 Offer next(129);s.generation=129;
 CHECK(fl::SharedCpuImportedPoolWin::adopt(s,next.d,next.fds,2,req,ops,false,bounded)==FL_BUSY);
 CHECK(bounded.generation()==128);pins.clear();
 s.sessionId.bytes[0]=9;CHECK(fl::SharedCpuImportedPoolWin::adopt(s,next.d,next.fds,2,req,ops,false,bounded)==FL_INVALID);s.sessionId.bytes[0]=1;
 CHECK(fl::SharedCpuImportedPoolWin::adopt(s,next.d,next.fds,2,req,ops,false,bounded)==FL_OK);
 }
 CHECK(fdCount()==baseline);
 puts("shared CPU imported pool: atomic failure, handle cleanup, kernel object aliases, stale epochs, retained retired mappings and producer rights passed");
}
