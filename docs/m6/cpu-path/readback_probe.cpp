#include "framelink.h"
#include <smmintrin.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
__attribute__((target("sse4.1"))) void stream(unsigned char*d,const unsigned char*s,size_t n){size_t i=0;for(;i+64<=n;i+=64){auto a=_mm_stream_load_si128((__m128i*)(s+i));auto b=_mm_stream_load_si128((__m128i*)(s+i+16));auto c=_mm_stream_load_si128((__m128i*)(s+i+32));auto e=_mm_stream_load_si128((__m128i*)(s+i+48));_mm_storeu_si128((__m128i*)(d+i),a);_mm_storeu_si128((__m128i*)(d+i+16),b);_mm_storeu_si128((__m128i*)(d+i+32),c);_mm_storeu_si128((__m128i*)(d+i+48),e);}for(;i+16<=n;i+=16)_mm_storeu_si128((__m128i*)(d+i),_mm_stream_load_si128((__m128i*)(s+i)));if(i<n)memcpy(d+i,s+i,n-i);}
int main(){CHECK(__builtin_cpu_supports("sse4.1"));puts("format,width,height,iteration,map_ms,method,copy_ms,minor_faults_unmeasured");
for(auto wh: {std::pair<unsigned,unsigned>{1920,1080},{3840,2160}})for(auto fmt:{FL_FORMAT_NV12,FL_FORMAT_BGRA8}){
 auto w=wh.first,h=wh.second;std::string name="cpu-probe-"+std::to_string((long long)now())+"-"+std::to_string(fmt);flChannel*c=nullptr,*p=nullptr;CHECK(flCreateChannelEx(name.c_str(),w,h,fmt,1,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&p)==FL_OK);
 for(unsigned n=0;n<8;n++){
  flBuffer b{};CHECK(flAcquireBuffer(p,&b,2000)==FL_OK);flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);
  for(unsigned z=0;z<m.planes;z++)for(unsigned y=0;y<m.plane[z].rows;y++)memset((char*)m.plane[z].pixels+y*m.plane[z].stride,31+z,m.plane[z].rowBytes);
  flImageUnmap(b.image);CHECK(flSubmit(p,&b,n)==FL_OK);flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);auto t=now();CHECK(flImageMapPlanes(f.image,&m)==FL_OK);double map=now()-t;
  size_t total=0;for(unsigned z=0;z<m.planes;z++)total+=m.plane[z].rowBytes*m.plane[z].rows;std::vector<unsigned char>dst(total,0),cached(total,7);
  for(unsigned method=0;method<3;method++){
   size_t off=0;t=now();
   for(unsigned z=0;z<m.planes;z++)for(unsigned y=0;y<m.plane[z].rows;y++){
    size_t bytes=m.plane[z].rowBytes;auto*src=(unsigned char*)m.plane[z].pixels+y*m.plane[z].stride;
    if(method==1){CHECK(((uintptr_t)src&15)==0);stream(dst.data()+off,src,bytes);}else memcpy(dst.data()+off,method==2?cached.data()+off:src,bytes);off+=bytes;
   }
   double elapsed=now()-t;off=0;
   for(unsigned z=0;z<m.planes;z++){size_t bytes=m.plane[z].rowBytes*m.plane[z].rows;CHECK(std::all_of(dst.begin()+off,dst.begin()+off+bytes,[=](unsigned char v){return v==(method==2?7:31+z);}));off+=bytes;}
   printf("%s,%u,%u,%u,%.6f,%s,%.6f,yes\n",fmt==FL_FORMAT_NV12?"NV12":"BGRA",w,h,n,map,method==0?"memcpy":method==1?"stream":"cached",elapsed);fflush(stdout);
  }
  flImageUnmap(f.image);CHECK(flRelease(c,&f)==FL_OK);
 }
 flClose(p);flClose(c);
}}
