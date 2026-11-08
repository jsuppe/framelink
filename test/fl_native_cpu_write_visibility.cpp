#include "framelink.h"
#include "framelink_sync_fd.h"
#ifdef __ANDROID__
#include "framelink_ahb.h"
using Native=AHardwareBuffer*;
#else
#include "framelink_dmabuf.h"
using Native=flDmabuf;
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned imageWidth,imageHeight;
#include "fl_native_benchmark_gpu.hpp"
// The same allocation is CPU-written, imported once, and GPU-read repeatedly.
// Header and corner-only checks cannot establish visibility of interior pixels.
int main(int argc,char**argv){
 const bool corrupt=argc==2&&!strcmp(argv[1],"--corrupt-interior");
 CHECK(argc==1||corrupt);
 for(unsigned w:{1920u,3840u}){
  imageWidth=w;imageHeight=w==1920?1080:2160;
  const auto name="cpu-write-visibility-"+std::to_string(getpid())+"-"+std::to_string(w);
  flChannel*c=nullptr,*p=nullptr;
#ifdef __ANDROID__
  const auto format=FL_FORMAT_RGBA8;
#else
  const auto format=FL_FORMAT_BGRA8;
#endif
  CHECK(flCreateChannelEx(name.c_str(),w,imageHeight,format,1,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
  CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
  {BenchmarkGpu gpu;gpu.start();
   for(unsigned n=0;n<16;n++){
    flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);flMappedImage m{};
    CHECK(flImageMapPlanes(b.image,&m)==FL_OK&&m.planes==1);
    for(unsigned y=0;y<imageHeight;y++)memset((char*)m.plane[0].pixels+y*m.plane[0].stride,n,w*4);
    if(corrupt)((unsigned char*)m.plane[0].pixels)[(imageHeight/2)*m.plane[0].stride+(w/2)*4]=n+1;
    flImageUnmap(b.image);CHECK(flSubmit(p,&b,n)==FL_OK);
    flFrame f{};uint64_t lease=0;CHECK(flAcquireFrame(c,&f,2000)==FL_OK&&f.ptsNs==n);
    CHECK(flLeaseFrame(c,&f,&lease)==FL_OK);unsigned char header[32]{};
    gpu.read(c,f,lease,header,sizeof(header),n);
    for(auto v:header)CHECK(v==n);
    gpu.verifyUniform(n);
   }
  }flClose(p);flClose(c);
 }
 puts("32 repeated CPU-write/GPU-read full-frame visibility checks passed");
}
