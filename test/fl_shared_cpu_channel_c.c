#include "framelink_shared_cpu_channel.h"
int main(void){
 flSharedCpuChannel*c=0;flSharedCpuLease*l=0;flMappedImage view={0};
 if(flSharedCpuOpenProducerFd(-1,100,0,0,0,&c)!=FL_INVALID||c)return 1;
 if(flSharedCpuAcquireWrite(0,0,&l,&view)!=FL_INVALID||l)return 2;
 if(flSharedCpuReleaseRead(0)!=FL_INVALID||flSharedCpuCancelWrite(0)!=FL_INVALID)return 3;
 flSharedCpuClose(0);return 0;
}
