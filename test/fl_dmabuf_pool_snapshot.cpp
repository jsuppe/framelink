// Linux consumer-pool snapshot (flSnapshotDmabufConsumerPool): owned fds of
// the exact original allocations, a pinned pool while any snapshot lives (new
// producer attach FL_BUSY, geometry requests answered with the current ring),
// import of the same allocations into a second ring, and snapshot lifetime
// beyond the source channel.
#include "framelink.h"
#include "framelink_dmabuf.h"
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
static ino_t inode(int fd){struct stat st{};CHECK(fd>=0&&fstat(fd,&st)==0);return st.st_ino;}
static flResult openProducer(const std::string&name,flChannel**p){
 flResult r=FL_NOT_FOUND;
 for(int n=0;n<400&&(r=flOpenProducer(name.c_str(),p))==FL_NOT_FOUND;n++)std::this_thread::sleep_for(std::chrono::milliseconds(5));
 return r;
}
// One frame through the ring; returns the allocation (inode) the consumer read.
static ino_t frame(flChannel*p,flChannel*c,unsigned char value,int64_t pts){
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);void*data=nullptr;uint64_t stride=0;
 CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
 for(uint32_t y=0;y<H;y++)std::memset(static_cast<char*>(data)+y*stride,value,W*4);
 flImageUnmap(b.image);CHECK(flSubmit(p,&b,pts)==FL_OK);
 flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK&&f.ptsNs==pts);
 flDmabuf d{};CHECK(flImageDmabuf(f.image,&d)==FL_OK);const ino_t id=inode(d.fd[0]);
 CHECK(flRelease(c,&f)==FL_OK);return id;
}
// Read the first row of an allocation through the snapshot's own fd.
static unsigned char firstByte(const flDmabuf&d){
 const size_t length=d.offset[0]+d.stride[0]*H;
 dma_buf_sync sync{DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ};CHECK(ioctl(d.fd[0],DMA_BUF_IOCTL_SYNC,&sync)==0);
 void*memory=mmap(nullptr,length,PROT_READ,MAP_SHARED,d.fd[0],0);CHECK(memory!=MAP_FAILED);
 const unsigned char value=static_cast<unsigned char*>(memory)[d.offset[0]];
 CHECK(munmap(memory,length)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;CHECK(ioctl(d.fd[0],DMA_BUF_IOCTL_SYNC,&sync)==0);
 return value;
}

int main(){
 const std::string name="pool-snapshot-"+std::to_string(getpid());
 flChannel*c=nullptr;
 if(flCreateChannelEx(name.c_str(),W,H,FL_FORMAT_BGRA8,POOL,FL_MAP_CPU,&c)!=FL_OK||!c){puts("no GBM device");return 77;}
 flDmabufPoolSnapshot*s=reinterpret_cast<flDmabufPoolSnapshot*>(1);
 CHECK(flSnapshotDmabufConsumerPool(nullptr,&s)==FL_INVALID&&!s);
 CHECK(flSnapshotDmabufConsumerPool(c,nullptr)==FL_INVALID);
 flChannel*p=nullptr;CHECK(openProducer(name,&p)==FL_OK);
 s=reinterpret_cast<flDmabufPoolSnapshot*>(1);
 CHECK(flSnapshotDmabufConsumerPool(p,&s)==FL_INVALID&&!s); // producer channels are rejected
 const ino_t read=frame(p,c,41,100);

 // Exact original allocations: distinct, and the frame just read is one of them.
 flDmabufPoolSnapshot*first=nullptr;CHECK(flSnapshotDmabufConsumerPool(c,&first)==FL_OK&&first);
 flChannelInfo info{},live{};CHECK(flQueryDmabufPoolSnapshot(first,&info)==FL_OK&&flQuery(c,&live)==FL_OK);
 CHECK(info.poolSize==POOL&&info.width==W&&info.height==H&&info.format==FL_FORMAT_BGRA8&&info.generation==live.generation);
 std::set<ino_t> slots;flDmabuf images[POOL]{};int readSlot=-1;
 for(uint32_t n=0;n<POOL;n++){
  CHECK(flDmabufPoolSnapshotImage(first,n,&images[n])==FL_OK&&images[n].planes==1);
  const ino_t id=inode(images[n].fd[0]);slots.insert(id);if(id==read)readSlot=int(n);
 }
 flDmabuf none{};CHECK(flDmabufPoolSnapshotImage(first,POOL,&none)==FL_INVALID);
 CHECK(flDmabufPoolSnapshotImage(nullptr,0,&none)==FL_INVALID&&flDmabufPoolSnapshotImage(first,0,nullptr)==FL_INVALID);
 CHECK(slots.size()==POOL&&readSlot>=0);
 CHECK(firstByte(images[readSlot])==41); // the snapshot fd names the written allocation

 // The same allocations imported into a second ring: no new storage.
 {flChannel*copy=nullptr;const std::string second=name+"-import";
  CHECK(flCreateChannelFromDmabufs(second.c_str(),W,H,FL_FORMAT_BGRA8,POOL,FL_MAP_CPU,images,&copy)==FL_OK&&copy);
  flChannel*writer=nullptr;CHECK(openProducer(second,&writer)==FL_OK);
  const ino_t imported=frame(writer,copy,57,200);CHECK(slots.count(imported));
  flClose(writer);flClose(copy);}

 // Pinned: the producer's geometry request is answered with the current ring.
 flChannelInfo before{},after{};CHECK(flQuery(p,&before)==FL_OK);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_BGRA8)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);
 CHECK(after.width==W&&after.height==H&&after.generation==before.generation);
 CHECK(flQuery(c,&live)==FL_OK&&live.width==W&&live.generation==info.generation);
 // Existing media continues while pinned.
 CHECK(slots.count(frame(p,c,63,300)));

 // Pinned: a newly attaching producer is refused, for every live snapshot.
 flDmabufPoolSnapshot*second=nullptr;CHECK(flSnapshotDmabufConsumerPool(c,&second)==FL_OK&&second);
 flClose(p);p=nullptr;
 CHECK(openProducer(name,&p)==FL_BUSY&&!p);
 flReleaseDmabufPoolSnapshot(first);
 CHECK(openProducer(name,&p)==FL_BUSY&&!p);
 flReleaseDmabufPoolSnapshot(second);

 // Unpinned: attach and reallocation work again.
 CHECK(openProducer(name,&p)==FL_OK&&p);
 CHECK(flRequestGeometry(p,128,64,FL_FORMAT_BGRA8)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);
 CHECK(after.width==128&&after.height==64&&after.generation>info.generation);

 // A snapshot outlives its source: fds stay valid after both ends close.
 flDmabufPoolSnapshot*last=nullptr;CHECK(flSnapshotDmabufConsumerPool(c,&last)==FL_OK);
 flClose(p);flClose(c);
 CHECK(flQueryDmabufPoolSnapshot(last,&info)==FL_OK&&info.width==128&&info.poolSize==POOL);
 for(uint32_t n=0;n<POOL;n++){flDmabuf d{};CHECK(flDmabufPoolSnapshotImage(last,n,&d)==FL_OK);inode(d.fd[0]);}
 flReleaseDmabufPoolSnapshot(last);flReleaseDmabufPoolSnapshot(nullptr);
 puts("DMA-BUF pool snapshot: exact allocations, same-allocation import, pinned geometry and attach, release and source-close lifetime passed");
 return 0;
}
