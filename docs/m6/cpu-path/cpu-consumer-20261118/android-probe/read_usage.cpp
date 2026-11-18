// AHB CPU read cost: CPU_READ_RARELY vs CPU_READ_OFTEN, same workload.
#include <android/hardware_buffer.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(){
 const uint64_t base=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
 for(auto [w,h]:{std::pair{1920u,1080u},std::pair{3840u,2160u}})
 for(int round=0;round<2;++round)
 for(uint64_t read:{(uint64_t)AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,(uint64_t)AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN}){
  AHardwareBuffer_Desc d{};d.width=w;d.height=h;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=base|read;
  AHardwareBuffer*b[2]{};for(auto*&x:b)CHECK(AHardwareBuffer_allocate(&d,&x)==0);
  AHardwareBuffer_Desc got{};AHardwareBuffer_describe(b[0],&got);
  std::vector<uint8_t> out(size_t(w)*h*4);std::vector<double> lockMs,copyMs;bool ok=true;
  for(unsigned n=0;n<60;++n){
   auto*x=b[n&1];void*p=nullptr;const uint8_t v=uint8_t(n*7+1);
   CHECK(AHardwareBuffer_lock(x,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&p)==0);
   for(unsigned y=0;y<h;++y)memset((uint8_t*)p+size_t(y)*got.stride*4,v,w*4);CHECK(AHardwareBuffer_unlock(x,nullptr)==0);
   const double t0=now();CHECK(AHardwareBuffer_lock(x,read==AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN?AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN:AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&p)==0);
   const double t1=now();
   if(got.stride==w)memcpy(out.data(),p,out.size());else for(unsigned y=0;y<h;++y)memcpy(out.data()+size_t(y)*w*4,(uint8_t*)p+size_t(y)*got.stride*4,w*4);
   const double t2=now();CHECK(AHardwareBuffer_unlock(x,nullptr)==0);
   for(size_t i=0;i<out.size();i+=4093)ok&=out[i]==v;
   lockMs.push_back(t1-t0);copyMs.push_back(t2-t1);
  }
  auto med=[](std::vector<double>v){std::sort(v.begin(),v.end());return v[v.size()/2];};
  auto mx=[](const std::vector<double>&v){return *std::max_element(v.begin(),v.end());};
  printf("%ux%u %s stride=%u usage=0x%llx: lock p50=%.3f copy p50=%.3f max=%.3f ms (%.0f MB/s) pixels=%s\n",w,h,
   read==AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN?"READ_OFTEN ":"READ_RARELY",got.stride,(unsigned long long)got.usage,med(lockMs),med(copyMs),mx(copyMs),
   out.size()/1e3/med(copyMs),ok?"ok":"MISMATCH");
  for(auto*x:b)AHardwareBuffer_release(x);
 }
}
