// Which usage selects the cached mapping (allocation vs lock), and the cost of a fresh output buffer.
#include <android/hardware_buffer.h>
#include <sys/mman.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static const uint64_t R=AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,O=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,W=AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
int main(){
 const unsigned w=1920,h=1080;const size_t bytes=size_t(w)*h*4;
 const uint64_t base=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|W;
 struct Case{const char*name;uint64_t alloc,lock;bool fresh;};
 for(int round=0;round<2;++round)
 for(Case c:{Case{"alloc RARELY lock W|RARELY (current)",R,W|R,false},Case{"alloc RARELY lock OFTEN",R,O,false},
             Case{"alloc OFTEN  lock W|RARELY",O,W|R,false},Case{"alloc OFTEN  lock OFTEN",O,O,false},
             Case{"alloc OFTEN  lock W|OFTEN",O,W|O,false},
             Case{"alloc RARELY lock W|RARELY, fresh output",R,W|R,true},Case{"alloc OFTEN  lock OFTEN, fresh output",O,O,true}}){
  AHardwareBuffer_Desc d{};d.width=w;d.height=h;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=base|c.alloc;
  AHardwareBuffer*b[2]{};for(auto*&x:b)CHECK(AHardwareBuffer_allocate(&d,&x)==0);
  std::vector<uint8_t> reused(bytes);std::vector<double> ms;bool ok=true;
  for(unsigned n=0;n<40;++n){
   auto*x=b[n&1];void*p=nullptr;const uint8_t v=uint8_t(n*7+1);
   CHECK(AHardwareBuffer_lock(x,W,-1,nullptr,&p)==0);memset(p,v,bytes);CHECK(AHardwareBuffer_unlock(x,nullptr)==0);
   const double t0=now();
   uint8_t*out=reused.data();
   if(c.fresh){out=(uint8_t*)mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(out!=MAP_FAILED);}
   CHECK(AHardwareBuffer_lock(x,c.lock,-1,nullptr,&p)==0);memcpy(out,p,bytes);CHECK(AHardwareBuffer_unlock(x,nullptr)==0);
   const double t1=now();
   for(size_t i=0;i<bytes;i+=4093)ok&=out[i]==v;
   if(c.fresh)munmap(out,bytes);
   ms.push_back(t1-t0);
  }
  std::sort(ms.begin(),ms.end());
  printf("%-42s p50=%6.2f max=%6.2f ms pixels=%s\n",c.name,ms[ms.size()/2],ms.back(),ok?"ok":"MISMATCH");
  for(auto*x:b)AHardwareBuffer_release(x);
 }
}
