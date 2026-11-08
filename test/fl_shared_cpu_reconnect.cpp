#include "framelink_shared_cpu_channel.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int fds(){DIR*d=opendir("/proc/self/fd");CHECK(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
static int vmas(){FILE*f=fopen("/proc/self/maps","r");CHECK(f);char line[2048];int n=0;while(fgets(line,sizeof(line),f))if(strstr(line,"memfd:framelink-shared-cpu"))++n;fclose(f);return n;}
static void checkPixels(const flMappedImage&v,unsigned n){CHECK(v.planes==1);const auto*p=static_cast<const unsigned char*>(v.plane[0].pixels);for(unsigned i=0;i<1024;i++)CHECK(p[i]==n);}
int main(){
 const int beforeFd=fds(),beforeVma=vmas();
 flSharedCpuLease*old=nullptr;flMappedImage oldView{};
 for(unsigned cycle=1;cycle<=100;cycle++){
  flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=cycle;s.videoTrackId=1;s.video={FL_FORMAT_BGRA8,16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=cycle;v.allocationId=i+1;v.allocationBytes=1024;v.planeStride[0]=64;}
  int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair));pid_t child=fork();CHECK(child>=0);
  if(!child){close(pair[0]);
   // Drop the child's inherited copy of the prior disconnected lease; it must
   // not be mistaken for a peer lease of the newly delegated connection.
   if(old)CHECK(flSharedCpuReleaseRead(old)==FL_DISCONNECTED);
   flSharedCpuChannel*c=nullptr;CHECK(flSharedCpuOpenProducerFd(pair[1],1000,&s,&req,0,&c)==FL_OK);
   flSharedCpuLease*l=nullptr;flMappedImage view{};CHECK(flSharedCpuAcquireWrite(c,0,&l,&view)==FL_OK);memset(view.plane[0].pixels,cycle,1024);
   CHECK(flSharedCpuSubmit(l,1,cycle*1000)==FL_OK);CHECK(flSharedCpuReceiveRelease(c)==FL_DISCONNECTED);
   flSharedCpuClose(c);_exit(0);
  }
  close(pair[1]);flSharedCpuChannel*c=nullptr;CHECK(flSharedCpuOpenConsumerFd(pair[0],1000,&s,&req,0,d,2,&c)==FL_OK);
  flSharedCpuLease*current=nullptr;flMappedImage view{};uint64_t seq=0;int64_t pts=0;
  CHECK(flSharedCpuReceive(c,&current,&view,&seq,&pts)==FL_OK);CHECK(seq==1&&pts==cycle*1000);checkPixels(view,cycle);
  if(old){checkPixels(oldView,cycle-1);CHECK(oldView.plane[0].pixels!=view.plane[0].pixels);CHECK(flSharedCpuReleaseRead(old)==FL_DISCONNECTED);old=nullptr;}
  // Retain both references across close; each is released explicitly. A dead
  // connection may not recycle this old storage into the next generation.
  CHECK(flSharedCpuRetainRead(current)==FL_OK);flSharedCpuClose(c);checkPixels(view,cycle);
  CHECK(flSharedCpuReleaseRead(current)==FL_DISCONNECTED);old=current;oldView=view;
  int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  CHECK(fds()==beforeFd+1);CHECK(vmas()==beforeVma+1);
 }
 CHECK(flSharedCpuReleaseRead(old)==FL_DISCONNECTED);CHECK(fds()==beforeFd);CHECK(vmas()==beforeVma);
 puts("shared CPU reconnect: 100 fresh epochs, old retained pixels isolated, held-peer death, exact FD/VMA baseline passed");
}
