#include "framelink.h"
#include "framelink_dmabuf.h"
#include "framelink_sync_fd.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s EGL=%x GL=%x\n",__LINE__,#x,eglGetError(),glGetError());exit(1);}}while(0)
static unsigned imageWidth,imageHeight;using Native=flDmabuf;
static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
#include "gpu.hpp"
int main(){puts("width,height,iteration,draw_finish_ms,readback_ms,verified");for(unsigned w:{1920u,3840u}){
 imageWidth=w;imageHeight=w==1920?1080:2160;flChannel*c=nullptr,*p=nullptr;
 char name[100];snprintf(name,sizeof(name),"gl-staging-%d-%u",getpid(),w);
 CHECK(flCreateChannelEx(name,w,imageHeight,FL_FORMAT_BGRA8,1,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name,&p)==FL_OK);
 {BenchmarkGpu gpu;gpu.start();fprintf(stderr,"GL_RENDERER=%s GL_VERSION=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
 for(unsigned n=0;n<8;n++){flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);
 for(unsigned y=0;y<imageHeight;y++)memset((char*)m.plane[0].pixels+y*m.plane[0].stride,n,w*4);
 flImageUnmap(b.image);CHECK(flSubmit(p,&b,n)==FL_OK);flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);gpu.read(c,f,0,nullptr,32,n);CHECK(flRelease(c,&f)==FL_OK);
 }}flClose(p);flClose(c);
}}
