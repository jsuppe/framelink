#include "framelink.h"
#include "framelink_memory.h"
#include "framelink_dmabuf.h"
#include <gbm.h>
#include <drm_fourcc.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int main(int argc,char**argv) {
 flFormat format=argc>1?flFormat(std::strtoul(argv[1],nullptr,10)):FL_FORMAT_NV12;
 uint32_t fourcc=format==FL_FORMAT_NV12?DRM_FORMAT_NV12:format==FL_FORMAT_RGBA8?DRM_FORMAT_ABGR8888:format==FL_FORMAT_RGB10A2?DRM_FORMAT_ABGR2101010:DRM_FORMAT_ARGB8888;
 int drm=-1;gbm_device* device=nullptr;
 for(int n=128;n<136&&!device;n++){std::string path="/dev/dri/renderD"+std::to_string(n);drm=open(path.c_str(),O_RDWR|O_CLOEXEC);if(drm>=0){device=gbm_create_device(drm);if(!device)close(drm);}}
 if(!device){puts("no GBM device");return 77;}
 gbm_bo* original[2]{};flDmabuf descriptors[2]{};
 for(int n=0;n<2;n++) {
  original[n]=gbm_bo_create(device,64,32,fourcc,GBM_BO_USE_LINEAR);CHECK(original[n]);
  auto& d=descriptors[n];d.planes=gbm_bo_get_plane_count(original[n]);d.fourcc=gbm_bo_get_format(original[n]);d.modifier=gbm_bo_get_modifier(original[n]);
  for(uint32_t p=0;p<d.planes;p++){d.fd[p]=gbm_bo_get_fd_for_plane(original[n],p);CHECK(d.fd[p]>=0);d.offset[p]=gbm_bo_get_offset(original[n],p);d.stride[p]=gbm_bo_get_stride_for_plane(original[n],p);}
 }
 std::string name="external-linux-"+std::to_string(getpid());flChannel *c=nullptr,*p=nullptr;
 flDmabuf aliases[]={descriptors[0],descriptors[0]};
 CHECK(flCreateChannelFromDmabufs(name.c_str(),64,32,format,2,FL_MAP_CPU,aliases,&c)==FL_INVALID);CHECK(!c);
 flDmabuf malformed[2]={descriptors[0],descriptors[1]};malformed[0].stride[0]=0;
 CHECK(flCreateChannelFromDmabufs(name.c_str(),64,32,format,2,FL_MAP_CPU,malformed,&c)==FL_INVALID);CHECK(!c);
 CHECK(flCreateChannelFromDmabufs(name.c_str(),64,32,format,2,FL_MAP_CPU,descriptors,&c)==FL_OK);
 for(int n=0;n<2;n++)for(uint32_t plane=0;plane<descriptors[n].planes;plane++)close(descriptors[n].fd[plane]);
 for(int n=0;n<100&&!p;n++){auto r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_NOT_FOUND);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(p);
 flChannelInfo before{},after{};CHECK(flQuery(p,&before)==FL_OK);CHECK(flRequestGeometry(p,128,64,format)==FL_OK);CHECK(flQuery(p,&after)==FL_OK);CHECK(after.generation==before.generation && after.width==64);
 for(unsigned frame=0;frame<3;frame++) {
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);flMappedImage map{};CHECK(flImageMapPlanes(b.image,&map)==FL_OK);
  for(unsigned plane=0;plane<map.planes;plane++)for(unsigned row=0;row<map.plane[plane].rows;row++)std::memset(static_cast<char*>(map.plane[plane].pixels)+row*map.plane[plane].stride,37+frame+plane,map.plane[plane].rowBytes);
  flImageUnmap(b.image);CHECK(flSubmit(p,&b,123+frame)==FL_OK);
  flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);CHECK(f.ptsNs==123+frame);
  flMemoryResourceInfo info{};info.version=FL_MEMORY_INFO_VERSION;CHECK(flImageMemoryInfo(c,f.image,&info,sizeof(info))==FL_OK);CHECK(info.interopCapabilities & FL_MEMORY_INTEROP_EXTERNAL_POOL);CHECK(!(info.knownFields & FL_MEMORY_INFO_NATIVE_USAGE));
  CHECK(flImageMapPlanes(f.image,&map)==FL_OK);
  for(unsigned plane=0;plane<map.planes;plane++)for(unsigned row=0;row<map.plane[plane].rows;row++)for(unsigned x=0;x<map.plane[plane].rowBytes;x++)CHECK(static_cast<unsigned char*>(map.plane[plane].pixels)[row*map.plane[plane].stride+x]==37+frame+plane);
  flImageUnmap(f.image);
  if(frame==0) {
   auto& d=descriptors[b.slot];
   for(unsigned plane=0;plane<d.planes;plane++) {
    int fd=gbm_bo_get_fd_for_plane(original[b.slot],plane);CHECK(fd>=0);
    uint32_t rows=plane?16:32,bytes=format==FL_FORMAT_NV12?64:256;size_t length=d.offset[plane]+d.stride[plane]*rows;
    dma_buf_sync sync{DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ};CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
    void* memory=mmap(nullptr,length,PROT_READ,MAP_SHARED,fd,0);CHECK(memory!=MAP_FAILED);
    for(unsigned row=0;row<rows;row++)for(unsigned x=0;x<bytes;x++)CHECK(static_cast<unsigned char*>(memory)[d.offset[plane]+row*d.stride[plane]+x]==37+plane);
    CHECK(munmap(memory,length)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);close(fd);
   }
   gbm_bo_destroy(original[0]);gbm_bo_destroy(original[1]);original[0]=original[1]=nullptr;
  }
  CHECK(flRelease(c,&f)==FL_OK);
 }
 flClose(p);flClose(c);gbm_device_destroy(device);close(drm);
 printf("external DMA-BUF format=%u: alias/layout rejection, immutable ring, original-allocation pixels and independent lifetime passed\n",unsigned(format));
}
