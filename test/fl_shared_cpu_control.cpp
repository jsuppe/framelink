#include "../src/fl_shared_cpu_control.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int fdCount(){DIR*d=opendir("/proc/self/fd");CHECK(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
static flSessionConfig config(){flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=7;s.videoTrackId=1;s.video={FL_FORMAT_BGRA8,16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;return s;}
static flMemoryRequirements requirements(){flMemoryRequirements r{};CHECK(flMemoryRequirementsInit(&r)==FL_MEMORY_OK);r.allowedResourceTypes=FL_MEMORY_SHARED_CPU;r.allowedOperations=FL_MEMORY_COPY;return r;}
int main(){
 const int baseline=fdCount();
 // Actual cross-process setup. Child receives no inherited payload allocation:
 // consumer creates the two allocations only after fork and negotiation.
 int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));
 pid_t child=fork();CHECK(child>=0);
 if(!child){close(fd[0]);fl::SharedCpuControl control(fl::Channel::adoptConnectedFd(fd[1],1000));
  auto r=requirements();r.requiredUsage=FL_MEMORY_CPU_WRITE;
  CHECK(control.negotiate(true,config(),r,FL_MEMORY_COPY)==FL_OK);
  fl::SharedCpuImportedPool pool;CHECK(control.receivePool(pool)==FL_OK);CHECK(pool.size()==2);
  for(unsigned i=0;i<2;i++){fl::SharedCpuAllocation::Mapping m;CHECK(pool.map(i,m)==FL_OK);auto*p=static_cast<unsigned char*>(m.view().plane[0].pixels);for(unsigned j=0;j<1024;j++)CHECK(p[j]==i+35);p[0]=91+i;}
  _exit(0);
 }
 close(fd[1]);{
 fl::SharedCpuControl control(fl::Channel::adoptConnectedFd(fd[0],1000));auto r=requirements();r.requiredUsage=FL_MEMORY_CPU_READ;
 CHECK(control.negotiate(false,config(),r,0)==FL_OK);
 flSharedCpuDescriptor desc[2]{};fl::SharedCpuAllocation owners[2];fl::SharedCpuAllocation::Mapping maps[2];int handles[2];
 for(unsigned i=0;i<2;i++){auto&d=desc[i];d.version=1;d.resourceType=FL_MEMORY_SHARED_CPU;d.format=FL_FORMAT_BGRA8;d.width=d.height=16;d.planeCount=1;d.generation=7;d.allocationId=i+1;d.allocationBytes=1024;d.planeStride[0]=64;
 CHECK(fl::SharedCpuAllocation::create(d,owners[i])==FL_OK);CHECK(owners[i].exportFd(true,handles[i])==FL_OK);CHECK(owners[i].map(true,maps[i])==FL_OK);memset(maps[i].view().plane[0].pixels,i+35,1024);}
 CHECK(control.sendPool(desc,handles,2)==FL_OK);for(int handle:handles)close(handle);
 int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 for(unsigned i=0;i<2;i++)CHECK(*static_cast<unsigned char*>(maps[i].view().plane[0].pixels)==91+i);
 CHECK(control.sendPool(desc,handles,2)==FL_INVALID); // cannot repeat setup
 }
 CHECK(fdCount()==baseline);
 // Wrong session, same endpoint role, copy/GPU policy: both ends fail closed.
 for(unsigned fault=0;fault<4;fault++){
 CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));child=fork();CHECK(child>=0);
 if(!child){close(fd[0]);fl::SharedCpuControl c(fl::Channel::adoptConnectedFd(fd[1],1000));auto s=config();auto r=requirements();
 if(fault==0)s.generation++;if(fault==2)r.allowedOperations=0;if(fault==3)r.requiredUsage=FL_MEMORY_GPU_SAMPLE;
 CHECK(c.negotiate(fault==1?false:true,s,r,0)!=FL_OK);CHECK(!c.agreed());_exit(0);}
 close(fd[1]);{fl::SharedCpuControl c(fl::Channel::adoptConnectedFd(fd[0],1000));CHECK(c.negotiate(false,config(),requirements(),FL_MEMORY_COPY)!=FL_OK);CHECK(!c.agreed());}
 int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 CHECK(fdCount()==baseline);
 // Malformed setup envelopes and unexpected SCM_RIGHTS must close all received
 // handles, never partially establish agreement or publish an imported pool.
 for(unsigned fault=0;fault<8;fault++)for(unsigned repeat=0;repeat<10;repeat++){
  CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));
  {
   auto peer=fl::Channel::adoptConnectedFd(fd[0],1000);
   fl::SharedCpuControl c(fl::Channel::adoptConnectedFd(fd[1],1000));
   std::array<uint8_t,256> hello{};memcpy(hello.data(),"FCH1",4);hello[4]=1;hello[8]=2;
   auto s=config();auto r=requirements();CHECK(flSessionEncode(&s,hello.data()+32,160)==FL_SESSION_OK);CHECK(flMemoryRequirementsEncode(&r,hello.data()+192,64)==FL_MEMORY_OK);
   int extra=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(extra>=0);
   if(fault==0)hello[0]='X';if(fault==1)hello[4]=2;if(fault==2)hello[16]=1;
   CHECK(peer.sendWithFds(fault==3?fl::MsgType::Hello:fl::MsgType::SharedCpuHello,hello.data(),hello.size()-(fault==4),fault==5?&extra:nullptr,fault==5?1:0));
   if(fault<6){CHECK(c.negotiate(true,s,r,0)==FL_INVALID);CHECK(!c.agreed());}
   else {
    CHECK(c.negotiate(true,s,r,0)==FL_OK);
    std::array<uint8_t,272> pool{};memcpy(pool.data(),"FCP1",4);pool[4]=1;pool[8]=2;
    int extras[2]={extra,extra};CHECK(peer.sendWithFds(fl::MsgType::SharedCpuPool,pool.data(),pool.size(),extras,fault==6?1:2));
    fl::SharedCpuImportedPool out;CHECK(c.receivePool(out)==FL_INVALID);CHECK(out.size()==0);CHECK(!c.agreed());
   }
   close(extra);
  }
  CHECK(fdCount()==baseline);
 }
 puts("shared CPU control: explicit peer agreement, cross-process pool/FD/pixel identity and fail-closed session/role/policy negatives passed");
}
