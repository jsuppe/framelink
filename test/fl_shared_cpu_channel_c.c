#include "framelink_shared_cpu_channel.h"
#ifdef _WIN32
#include "framelink_shared_cpu_win.h"
#endif
int main(void){
 flSharedCpuChannel*c=0;flSharedCpuLease*l=0;flMappedImage view={0};
#ifdef _WIN32
 if(flSharedCpuOpenProducerPipe(0,0,0,100,0,0,0,0,&c)!=FL_INVALID||c)return 1;
#else
 if(flSharedCpuOpenProducerFd(-1,100,0,0,0,&c)!=FL_INVALID||c)return 1;
#endif
 if(flSharedCpuAcquireWrite(0,0,&l,&view)!=FL_INVALID||l)return 2;
 if(flSharedCpuReleaseRead(0)!=FL_INVALID||flSharedCpuCancelWrite(0)!=FL_INVALID)return 3;
 flSharedCpuClose(0);return 0;
}
