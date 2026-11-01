#include "framelink.h"
#include "framelink_memory.h"
#include "framelink_ahb.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int main(int argc,char**argv) {
 flFormat format=argc>1?flFormat(std::strtoul(argv[1],nullptr,10)):FL_FORMAT_NV12;bool broker=argc>2;
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);auto getId=reinterpret_cast<GetId>(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));
 if(!getId){puts("external pool requires allocation IDs (API31+)");return 77;}
 AHardwareBuffer_Desc d{};d.width=64;d.height=32;d.layers=1;
 d.format=format==FL_FORMAT_NV12?AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:format==FL_FORMAT_RGB10A2?AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
 d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
 AHardwareBuffer* original[2]{};uint64_t ids[2]{};
 for(int n=0;n<2;n++){CHECK(AHardwareBuffer_allocate(&d,&original[n])==0);CHECK(getId(original[n],&ids[n])==0);}
 std::string name="external-ahb-"+std::to_string(getpid());flChannel *c=nullptr,*p=nullptr;
 int pair[2];CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);
 CHECK(AHardwareBuffer_sendHandleToUnixSocket(original[0],pair[0])==0);
 AHardwareBuffer* duplicate=nullptr;CHECK(AHardwareBuffer_recvHandleFromUnixSocket(pair[1],&duplicate)==0);close(pair[0]);close(pair[1]);
 AHardwareBuffer* aliases[]={original[0],duplicate};
 CHECK(flCreateChannelFromHardwareBuffers(name.c_str(),64,32,format,2,FL_MAP_CPU,aliases,&c)==FL_INVALID);CHECK(!c);AHardwareBuffer_release(duplicate);
 CHECK(flCreateChannelFromHardwareBuffers(name.c_str(),128,32,format,2,FL_MAP_CPU,original,&c)==FL_FORMAT_UNSUPPORTED);CHECK(!c);
 CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);
 CHECK(flCreateChannelFromSocketFdWithHardwareBuffers(pair[0],64,32,format,2,FL_MAP_CPU,2000,nullptr,&c)==FL_INVALID);CHECK(!c);
 errno=0;CHECK(fcntl(pair[0],F_GETFD)==-1 && errno==EBADF);close(pair[1]);
 if(broker) {
  CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);
  CHECK(flCreateChannelFromSocketFdWithHardwareBuffers(pair[0],64,32,format,2,FL_MAP_CPU,2000,original,&c)==FL_OK);
  CHECK(flOpenProducerFromSocketFd(pair[1],2000,&p)==FL_OK);
 } else {
  CHECK(flCreateChannelFromHardwareBuffers(name.c_str(),64,32,format,2,FL_MAP_CPU,original,&c)==FL_OK);
  for(int n=0;n<100&&!p;n++){auto r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_NOT_FOUND);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(p);
 }
 flChannelInfo before{},after{};CHECK(flQuery(p,&before)==FL_OK);CHECK(flRequestGeometry(p,128,64,format)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);CHECK(after.generation==before.generation && after.width==64);
 for(unsigned frame=0;frame<3;frame++) {
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);flMappedImage map{};CHECK(flImageMapPlanes(b.image,&map)==FL_OK);
  uint64_t id=0;CHECK(getId(flImageAHardwareBuffer(b.image),&id)==0);CHECK(id==ids[b.slot]);
  for(unsigned plane=0;plane<map.planes;plane++)for(unsigned row=0;row<map.plane[plane].rows;row++)std::memset(static_cast<char*>(map.plane[plane].pixels)+row*map.plane[plane].stride,37+frame+plane,map.plane[plane].rowBytes);
  flImageUnmap(b.image);CHECK(flSubmit(p,&b,123+frame)==FL_OK);
  flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);CHECK(f.ptsNs==123+frame);
  CHECK(getId(flImageAHardwareBuffer(f.image),&id)==0);CHECK(id==ids[b.slot]);
  flMemoryResourceInfo info{};info.version=FL_MEMORY_INFO_VERSION;CHECK(flImageMemoryInfo(c,f.image,&info,sizeof(info))==FL_OK);CHECK(info.interopCapabilities & FL_MEMORY_INTEROP_EXTERNAL_POOL);
  CHECK(flImageMapPlanes(f.image,&map)==FL_OK);
  for(unsigned plane=0;plane<map.planes;plane++)for(unsigned row=0;row<map.plane[plane].rows;row++)for(unsigned x=0;x<map.plane[plane].rowBytes;x++)CHECK(static_cast<unsigned char*>(map.plane[plane].pixels)[row*map.plane[plane].stride+x]==37+frame+plane);
  flImageUnmap(f.image);
  if(frame==0) {
   if(format==FL_FORMAT_NV12) {
    AHardwareBuffer_Planes planes{};CHECK(AHardwareBuffer_lockPlanes(original[b.slot],AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&planes)==0);CHECK(planes.planeCount==3);
    for(unsigned plane=0;plane<3;plane++)for(unsigned row=0;row<(plane?16u:32u);row++)for(unsigned x=0;x<(plane?32u:64u);x++)CHECK(static_cast<unsigned char*>(planes.planes[plane].data)[row*planes.planes[plane].rowStride+x*planes.planes[plane].pixelStride]==(plane?38:37));
   } else {
    void* pixels=nullptr;AHardwareBuffer_Desc actual{};AHardwareBuffer_describe(original[b.slot],&actual);CHECK(AHardwareBuffer_lock(original[b.slot],AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&pixels)==0);
    for(unsigned row=0;row<32;row++)for(unsigned x=0;x<256;x++)CHECK(static_cast<unsigned char*>(pixels)[row*actual.stride*4+x]==37);
   }
   CHECK(AHardwareBuffer_unlock(original[b.slot],nullptr)==0);
   AHardwareBuffer_release(original[0]);AHardwareBuffer_release(original[1]);original[0]=original[1]=nullptr;
  }
  CHECK(flRelease(c,&f)==FL_OK);
 }
 flClose(p);flClose(c);
 printf("external AHB format=%u broker=%d: allocation-ID alias rejection, FD ownership, immutable ring, original pixels and independent lifetime passed\n",unsigned(format),broker);
}
