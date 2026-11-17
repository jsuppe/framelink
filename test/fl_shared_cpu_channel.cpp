#include "framelink_shared_cpu_channel.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <poll.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int countFds(){auto*d=opendir("/proc/self/fd");CHECK(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
static void bytes(const flMappedImage&v,unsigned value,bool write){for(unsigned p=0;p<v.planes;p++)for(unsigned y=0;y<v.plane[p].rows;y++){auto*b=static_cast<unsigned char*>(v.plane[p].pixels)+y*v.plane[p].stride;for(unsigned x=0;x<v.plane[p].rowBytes;x++)if(write)b[x]=value;else CHECK(b[x]==value);}}
int main(){
 const int baseline=countFds();
 for(bool nv12:{false,true}){
  flSessionConfig s{};s.version=2;s.sessionId.bytes[0]=1;s.clockId.bytes[0]=2;s.generation=7;s.videoTrackId=1;s.video={nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8,16,16,60,1};s.videoQueueFrames=1;s.connectTimeoutMs=s.idleTimeoutMs=1000;
  flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
  int fd[2],sync[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sync));
  pid_t child=fork();CHECK(child>=0);
  if(!child){close(fd[0]);close(sync[0]);int observe=dup(fd[1]);CHECK(observe>=0);flSharedCpuChannel*c=nullptr;CHECK(flSharedCpuOpenProducerFdEx(fd[1],1000,&s,&req,0,nv12?FL_SHARED_CPU_FRAME_METADATA:0,&c)==FL_OK);
   flSharedCpuLease*heldWrite=nullptr;flMappedImage heldView{};
   CHECK(flSharedCpuAcquireWrite(c,1,&heldWrite,&heldView)==FL_OK);CHECK(flSharedCpuCancelWrite(heldWrite)==FL_OK);
   CHECK(flSharedCpuAcquireWrite(c,1,&heldWrite,&heldView)==FL_OK);bytes(heldView,42,true);CHECK(flSharedCpuReleaseRead(heldWrite)==FL_INVALID);
   for(unsigned seq=1;seq<=8;seq++){
    flSharedCpuLease*l=nullptr;flMappedImage v{};CHECK(flSharedCpuAcquireWrite(c,0,&l,&v)==FL_OK);
    flSharedCpuLease*unchanged=nullptr;flMappedImage empty{};CHECK(flSharedCpuAcquireWrite(c,0,&unchanged,&empty)==FL_BUSY);CHECK(!unchanged&&!empty.planes);
    CHECK(flSharedCpuRetainRead(l)==FL_INVALID);bytes(v,seq,true);if(nv12){flFrameMetadata meta{};meta.version=1;meta.flags=FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|(seq==4?FL_FRAME_DISCONT:0);meta.dtsNs=seq*1000-10;meta.durationNs=1000;meta.streamGeneration=77;
     CHECK(flSharedCpuSubmitWithMetadata(l,seq,seq*1000,&meta)==FL_OK);
    }else CHECK(flSharedCpuSubmit(l,seq,seq*1000)==FL_OK);
    CHECK(flSharedCpuAcquireWrite(c,0,&unchanged,&empty)==FL_BUSY);
    if(seq==1){char b;CHECK(read(sync[1],&b,1)==1);pollfd p{observe,POLLIN,0};CHECK(poll(&p,1,30)==0);CHECK(write(sync[1],"x",1)==1);}
    if(seq==8){CHECK(flSharedCpuReceiveRelease(c)==FL_DISCONNECTED);break;}
    CHECK(flSharedCpuReceiveRelease(c)==FL_OK);
   }
   flSharedCpuClose(c);bytes(heldView,42,false);CHECK(flSharedCpuCancelWrite(heldWrite)==FL_DISCONNECTED);close(observe);close(sync[1]);_exit(0);
  }
  close(fd[1]);close(sync[1]);flSharedCpuDescriptor d[2]{};
  for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=s.video.format;v.width=v.height=16;v.planeCount=nv12?2:1;v.generation=7;v.allocationId=i+1;v.planeStride[0]=nv12?16:64;v.allocationBytes=v.planeStride[0]*16;
   if(nv12){v.planeOffset[1]=v.allocationBytes;v.planeStride[1]=16;v.allocationBytes+=128;}}
  flSharedCpuChannel*c=nullptr;CHECK(flSharedCpuOpenConsumerFdEx(fd[0],1000,&s,&req,0,d,2,nv12?FL_SHARED_CPU_FRAME_METADATA:0,&c)==FL_OK);
  for(unsigned seq=1;seq<=8;seq++){
   flSharedCpuLease*l=nullptr;flMappedImage v{};uint64_t actual=0;int64_t pts=0;CHECK(flSharedCpuReceive(c,&l,&v,&actual,&pts)==FL_OK);CHECK(actual==seq&&pts==seq*1000);bytes(v,seq,false);
   flFrameMetadata meta{};CHECK(flSharedCpuReadMetadata(l,&meta)==FL_OK);
   if(nv12){CHECK(meta.version==1&&meta.dtsNs==seq*1000-10&&meta.durationNs==1000&&meta.streamGeneration==77);CHECK(meta.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|(seq==4?FL_FRAME_DISCONT:0)));}else CHECK(meta.version==0&&meta.streamGeneration==0);
   CHECK(flSharedCpuCancelWrite(l)==FL_INVALID);CHECK(flSharedCpuSubmit(l,99,99)==FL_INVALID);
   if(seq==1){CHECK(flSharedCpuRetainRead(l)==FL_OK);CHECK(flSharedCpuReleaseRead(l)==FL_OK);CHECK(write(sync[0],"x",1)==1);char b;CHECK(read(sync[0],&b,1)==1);bytes(v,seq,false);}
   if(seq==8){flSharedCpuClose(c);c=nullptr;bytes(v,seq,false);CHECK(flSharedCpuReleaseRead(l)==FL_DISCONNECTED);}else CHECK(flSharedCpuReleaseRead(l)==FL_OK);
  }
  close(sync[0]);int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);CHECK(countFds()==baseline);
 }
 // Invalid factory parameters still consume FD and preserve output.
 int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,fd));flSharedCpuChannel*out=nullptr;CHECK(flSharedCpuOpenProducerFd(fd[0],0,nullptr,nullptr,0,&out)==FL_INVALID);CHECK(!out);close(fd[1]);CHECK(countFds()==baseline);
 puts("shared CPU public channel: BGRA/NV12 frames, reserved writes, retained readers delay release, close-held views and FD cleanup passed");
}
