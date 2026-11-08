#include "framelink_shared_cpu_channel.h"
#include "../src/fl_ipc.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int fds(){auto*d=opendir("/proc/self/fd");CHECK(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
int main(){
 const int baseline=fds();
 for(unsigned mode=0;mode<11;mode++){
  flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=7;s.videoTrackId=1;s.video={FL_FORMAT_BGRA8,16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));pid_t child=fork();CHECK(child>=0);
  if(!child){
   close(fd[0]);int raw=dup(fd[1]);CHECK(raw>=0);flSharedCpuChannel*c=nullptr;
   CHECK(flSharedCpuOpenProducerFdEx(fd[1],1000,&s,&req,0,FL_SHARED_CPU_ORDERED_EOS|(mode==10?FL_SHARED_CPU_FRAME_METADATA:0),&c)==FL_OK);
   if((mode==0||mode==10)){
    flSharedCpuLease*l=nullptr;flMappedImage v{};
    for(unsigned i=0;i<2;i++){
     CHECK(flSharedCpuAcquireWrite(c,i,&l,&v)==FL_OK);CHECK(flSharedCpuSendEos(c)==FL_BUSY);
     memset(v.plane[0].pixels,41+i,1024);if(mode==10){flFrameMetadata m{};m.version=1;m.streamGeneration=99;CHECK(flSharedCpuSubmitWithMetadata(l,i+1,100+i,&m)==FL_OK);}
     else CHECK(flSharedCpuSubmit(l,i+1,100+i)==FL_OK);
    }
    CHECK(flSharedCpuSendEos(c)==FL_OK);CHECK(flSharedCpuSendEos(c)==FL_INVALID);
    l=nullptr;CHECK(flSharedCpuAcquireWrite(c,0,&l,&v)==FL_BUSY&&!l);
    CHECK(flSharedCpuReceiveRelease(c)==FL_OK);CHECK(flSharedCpuReceiveRelease(c)==FL_OK);
    CHECK(flSharedCpuAcquireWrite(c,0,&l,&v)==FL_BUSY&&!l);
   }else if(mode==1)CHECK(flSharedCpuSendEos(c)==FL_OK);
   else if(mode>=3){
    auto peer=fl::Channel::adoptConnectedFd(raw,1000);raw=-1;
    unsigned char wire[32]{};memcpy(wire,"FCE1",4);wire[4]=1;wire[8]=7;
    if(mode==3)wire[0]='X';if(mode==4)wire[4]=2;if(mode==5)wire[8]=8;
    if(mode==6)wire[16]=1;if(mode==7)wire[31]=1;
    int extra=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(extra>=0);
    CHECK(peer.sendWithFds(fl::MsgType::SharedCpuEos,wire,mode==8?31:32,mode==9?&extra:nullptr,mode==9?1:0));close(extra);
   }
   flSharedCpuClose(c);if(raw>=0)close(raw);_exit(0);
  }
  close(fd[1]);flSharedCpuDescriptor d[2]{};
  for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=7;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=1024;}
  flSharedCpuChannel*c=nullptr;CHECK(flSharedCpuOpenConsumerFdEx(fd[0],1000,&s,&req,0,d,2,FL_SHARED_CPU_ORDERED_EOS|(mode==10?FL_SHARED_CPU_FRAME_METADATA:0),&c)==FL_OK);
  flSharedCpuLease*held[2]{};flMappedImage views[2]{};
  if((mode==0||mode==10))for(unsigned i=0;i<2;i++){
   uint64_t seq=0;int64_t pts=0;int eos=9;
   CHECK(flSharedCpuReceiveEvent(c,&held[i],&views[i],&seq,&pts,&eos)==FL_OK);
   CHECK(!eos&&seq==i+1&&pts==100+i);
  }
  flSharedCpuLease*l=nullptr;flMappedImage view{};uint64_t seq=999;int64_t pts=888;int eos=9;
  CHECK(flSharedCpuReceive(c,&l,&view,&seq,&pts)==FL_INVALID);
  auto r=flSharedCpuReceiveEvent(c,&l,&view,&seq,&pts,&eos);
  CHECK(!l&&!view.planes&&seq==999&&pts==888);
  if(mode<2||mode==10){CHECK(r==FL_OK&&eos==1);eos=9;CHECK(flSharedCpuReceiveEvent(c,&l,&view,&seq,&pts,&eos)==FL_OK&&eos==1);}
  else CHECK(r==(mode==2?FL_DISCONNECTED:FL_INVALID)&&eos==9);
  if((mode==0||mode==10))for(int i=1;i>=0;i--){for(unsigned j=0;j<1024;j++)CHECK(static_cast<unsigned char*>(views[i].plane[0].pixels)[j]==41+i);CHECK(flSharedCpuReleaseRead(held[i])==FL_OK);}
  flSharedCpuClose(c);int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);CHECK(fds()==baseline);
 }
 puts("CPU EOS: ordered frames, retained leases/reverse drain, empty stream, disconnect distinction and seven hostile packets passed");
}
