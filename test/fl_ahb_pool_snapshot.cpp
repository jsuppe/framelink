// Android consumer-pool snapshot (flSnapshotAHardwareBufferConsumerPool): owned
// references to the exact original allocations, a pinned pool while any
// snapshot lives (new producer attach FL_BUSY, geometry requests answered with
// the current ring), adoption of the same allocations into a second ring, and
// snapshot lifetime beyond the source channel. Identity is the AHardwareBuffer
// allocation ID (API 31+).
#include "framelink.h"
#include "framelink_ahb.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}} while(0)

static constexpr uint32_t W=64,H=32,POOL=3;
using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
static GetId getId=nullptr;
static uint64_t id(AHardwareBuffer*b){uint64_t v=0;CHECK(b&&getId(b,&v)==0);return v;}
static flResult openProducer(const std::string&name,flChannel**p){
 flResult r=FL_NOT_FOUND;
 for(int n=0;n<400&&(r=flOpenProducer(name.c_str(),p))==FL_NOT_FOUND;n++)std::this_thread::sleep_for(std::chrono::milliseconds(5));
 return r;
}
// One frame through the ring; returns the allocation the consumer read.
static uint64_t frame(flChannel*p,flChannel*c,unsigned char value,int64_t pts){
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);void*data=nullptr;uint64_t stride=0;
 CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
 for(uint32_t y=0;y<H;y++)std::memset(static_cast<char*>(data)+y*stride,value,W*4);
 flImageUnmap(b.image);CHECK(flSubmit(p,&b,pts)==FL_OK);
 flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK&&f.ptsNs==pts);
 const uint64_t allocation=id(flImageAHardwareBuffer(f.image));
 CHECK(flRelease(c,&f)==FL_OK);return allocation;
}
// Read the first byte of an allocation through the snapshot's own reference.
static unsigned char firstByte(AHardwareBuffer*b){
 void*data=nullptr;CHECK(AHardwareBuffer_lock(b,AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&data)==0&&data);
 const unsigned char value=*static_cast<unsigned char*>(data);CHECK(AHardwareBuffer_unlock(b,nullptr)==0);return value;
}

int main(){
 getId=reinterpret_cast<GetId>(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));
 if(!getId){puts("SKIP: allocation IDs need API 31+");return 77;}
 const std::string name="ahb-pool-snapshot-"+std::to_string(getpid());
 flChannel*c=nullptr;CHECK(flCreateChannelEx(name.c_str(),W,H,FL_FORMAT_RGBA8,POOL,FL_MAP_CPU,&c)==FL_OK&&c);
 flAHardwareBufferPoolSnapshot*s=reinterpret_cast<flAHardwareBufferPoolSnapshot*>(1);
 CHECK(flSnapshotAHardwareBufferConsumerPool(nullptr,&s)==FL_INVALID&&!s);
 CHECK(flSnapshotAHardwareBufferConsumerPool(c,nullptr)==FL_INVALID);
 flChannel*p=nullptr;CHECK(openProducer(name,&p)==FL_OK);
 s=reinterpret_cast<flAHardwareBufferPoolSnapshot*>(1);
 CHECK(flSnapshotAHardwareBufferConsumerPool(p,&s)==FL_INVALID&&!s); // producer channels are rejected
 const uint64_t read=frame(p,c,41,100);

 // Exact original allocations: distinct, and the frame just read is one of them.
 flAHardwareBufferPoolSnapshot*first=nullptr;CHECK(flSnapshotAHardwareBufferConsumerPool(c,&first)==FL_OK&&first);
 flChannelInfo info{},live{};CHECK(flQueryAHardwareBufferPoolSnapshot(first,&info)==FL_OK&&flQuery(c,&live)==FL_OK);
 CHECK(info.poolSize==POOL&&info.width==W&&info.height==H&&info.format==FL_FORMAT_RGBA8&&info.generation==live.generation);
 std::set<uint64_t> slots;AHardwareBuffer*buffers[POOL]{};int readSlot=-1;
 for(uint32_t n=0;n<POOL;n++){
  buffers[n]=flAHardwareBufferPoolSnapshotBuffer(first,n);CHECK(buffers[n]);
  const uint64_t allocation=id(buffers[n]);slots.insert(allocation);if(allocation==read)readSlot=int(n);
 }
 CHECK(!flAHardwareBufferPoolSnapshotBuffer(first,POOL)&&!flAHardwareBufferPoolSnapshotBuffer(nullptr,0));
 CHECK(flQueryAHardwareBufferPoolSnapshot(nullptr,&info)==FL_INVALID&&flQueryAHardwareBufferPoolSnapshot(first,nullptr)==FL_INVALID);
 CHECK(slots.size()==POOL&&readSlot>=0);
 CHECK(firstByte(buffers[readSlot])==41); // the snapshot reference names the written allocation

 // The same allocations adopted by a second ring: no new storage.
 {flChannel*copy=nullptr;const std::string second=name+"-import";
  CHECK(flCreateChannelFromHardwareBuffers(second.c_str(),W,H,FL_FORMAT_RGBA8,POOL,FL_MAP_CPU,buffers,&copy)==FL_OK&&copy);
  flChannel*writer=nullptr;CHECK(openProducer(second,&writer)==FL_OK);
  CHECK(slots.count(frame(writer,copy,57,200)));
  flClose(writer);flClose(copy);}

 // Pinned: the producer's geometry request is answered with the current ring.
 flChannelInfo before{},after{};CHECK(flQuery(p,&before)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);
 CHECK(after.width==W&&after.height==H&&after.generation==before.generation);
 CHECK(flQuery(c,&live)==FL_OK&&live.width==W&&live.generation==info.generation);
 // Existing media continues while pinned.
 CHECK(slots.count(frame(p,c,63,300)));

 // Pinned: a newly attaching producer is refused, for every live snapshot.
 flAHardwareBufferPoolSnapshot*second=nullptr;CHECK(flSnapshotAHardwareBufferConsumerPool(c,&second)==FL_OK&&second);
 flClose(p);p=nullptr;
 CHECK(openProducer(name,&p)==FL_BUSY&&!p);
 flReleaseAHardwareBufferPoolSnapshot(first);
 CHECK(openProducer(name,&p)==FL_BUSY&&!p);
 flReleaseAHardwareBufferPoolSnapshot(second);

 // Unpinned: attach and reallocation work again.
 CHECK(openProducer(name,&p)==FL_OK&&p);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_RGBA8)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);
 CHECK(after.width==128&&after.height==64&&after.generation>info.generation);

 // A snapshot outlives its source: references stay valid after both ends close.
 flAHardwareBufferPoolSnapshot*last=nullptr;CHECK(flSnapshotAHardwareBufferConsumerPool(c,&last)==FL_OK);
 flClose(p);flClose(c);
 CHECK(flQueryAHardwareBufferPoolSnapshot(last,&info)==FL_OK&&info.width==128&&info.poolSize==POOL);
 for(uint32_t n=0;n<POOL;n++){auto*b=flAHardwareBufferPoolSnapshotBuffer(last,n);AHardwareBuffer_Desc d{};AHardwareBuffer_describe(b,&d);CHECK(d.width==128&&d.height==64);id(b);}
 flReleaseAHardwareBufferPoolSnapshot(last);flReleaseAHardwareBufferPoolSnapshot(nullptr);
 puts("AHardwareBuffer pool snapshot: exact allocations, same-allocation adoption, pinned geometry and attach, release and source-close lifetime passed");
 return 0;
}
