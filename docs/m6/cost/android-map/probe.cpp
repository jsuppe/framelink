#include "framelink.h"
#include "framelink_ahb.h"
#include <android/hardware_buffer.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static double now(clockid_t clock=CLOCK_MONOTONIC){timespec t{};clock_gettime(clock,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static void report(const char*label,std::vector<double> v){std::sort(v.begin(),v.end());double sum=0;for(auto x:v)sum+=x;printf("%s.n=%zu\n%s.mean_ms=%.6f\n%s.p95_ms=%.6f\n",label,v.size(),label,sum/v.size(),label,v[size_t(.95*(v.size()-1))]);}
int main(int argc,char**argv){CHECK(argc==6);unsigned w=atoi(argv[1]),h=atoi(argv[2]),n=atoi(argv[3]);bool nv12=!strcmp(argv[4],"NV12");const char*mode=argv[5];bool raw=!strcmp(mode,"raw"),copy=!strcmp(mode,"copy");CHECK(w&&h&&n>20&&(raw||copy||!strcmp(mode,"map")));
 flChannel*c=nullptr,*p=nullptr;char name[80];snprintf(name,sizeof name,"map-cost-%d",getpid());
 CHECK(flCreateChannelEx(name,w,h,nv12?FL_FORMAT_NV12:FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name,&p)==FL_OK);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);flMappedImage initial{};CHECK(flImageMapPlanes(b.image,&initial)==FL_OK);
 for(unsigned q=0;q<initial.planes;++q)for(unsigned y=0;y<initial.plane[q].rows;++y)memset((char*)initial.plane[q].pixels+y*initial.plane[q].stride,q?128:64,initial.plane[q].rowBytes);
 flImageUnmap(b.image);auto*ahb=flImageAHardwareBuffer(b.image);CHECK(ahb);AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(ahb,&desc);
 printf("scope=single-acquired-buffer-map-control-no-av\nformat=%s\nmode=%s\nwidth=%u\nheight=%u\nnative.format=%u\nnative.usage=%llu\n",argv[4],mode,w,h,desc.format,(unsigned long long)desc.usage);
 AHardwareBuffer_Planes layout{};CHECK(AHardwareBuffer_lockPlanes(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&layout)==0);
 printf("native.planes=%u\n",layout.planeCount);for(unsigned q=0;q<layout.planeCount;++q)printf("native.plane%u=%u,%u\n",q,layout.planes[q].rowStride,layout.planes[q].pixelStride);
 if(layout.planeCount==3)printf("native.uv_delta=%lld\n",(long long)((intptr_t)layout.planes[2].data-(intptr_t)layout.planes[1].data));CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0);
 std::vector<unsigned char> output(size_t(w)*h*(nv12?3:8)/2);std::vector<double> maps,copies,unmaps;maps.reserve(n);copies.reserve(n);unmaps.reserve(n);double cpu=0;
 for(unsigned i=0;i<n+20;++i){double cpu0=now(CLOCK_THREAD_CPUTIME_ID),t0=now();flMappedImage view{};AHardwareBuffer_Planes planes{};
  if(raw){CHECK(AHardwareBuffer_lockPlanes(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&planes)==0);}else CHECK(flImageMapPlanes(b.image,&view)==FL_OK);
  double t1=now();size_t offset=0;if(copy)for(unsigned q=0;q<view.planes;++q)for(unsigned y=0;y<view.plane[q].rows;++y){memcpy(output.data()+offset,(char*)view.plane[q].pixels+y*view.plane[q].stride,view.plane[q].rowBytes);offset+=view.plane[q].rowBytes;}
  double t2=now();if(raw){CHECK(AHardwareBuffer_unlock(ahb,nullptr)==0);}else flImageUnmap(b.image);double t3=now();
  if(i>=20){maps.push_back(t1-t0);copies.push_back(t2-t1);unmaps.push_back(t3-t2);cpu+=now(CLOCK_THREAD_CPUTIME_ID)-cpu0;}
  if(copy){CHECK(offset==output.size());CHECK(output.front()==64&&output.back()==(nv12?128:64));}
 }
 // Verify logical content after all raw/public lock cycles, not merely pointers.
 CHECK(flImageMapPlanes(b.image,&initial)==FL_OK);for(unsigned q=0;q<initial.planes;++q)for(unsigned y=0;y<initial.plane[q].rows;++y){auto*row=(unsigned char*)initial.plane[q].pixels+y*initial.plane[q].stride;CHECK(row[0]==(q?128:64)&&row[initial.plane[q].rowBytes-1]==(q?128:64));}flImageUnmap(b.image);
 report("map",maps);report("copy",copies);report("unmap",unmaps);printf("cpu.mean_ms=%.6f\ncontent=verified\n",cpu/n);
 CHECK(flCancelBuffer(p,&b)==FL_OK);flClose(p);flClose(c);return 0;
}
