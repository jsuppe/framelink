#include "framelink.h"
#include "framelink_ahb.h"
#include "framelink_sync_fd.h"
#include <android/hardware_buffer.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Clock=std::chrono::steady_clock;
static bool closed(int fd){errno=0;return fcntl(fd,F_GETFD)==-1 && errno==EBADF;}
static void pair(int* fd){CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));}
static void failures(){
 flChannel* ch=reinterpret_cast<flChannel*>(1);int fd[2];
 CHECK(flOpenProducerFromSocketFd(-1,100,&ch)==FL_INVALID && !ch);
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
 CHECK(flCreateChannelFromSocketFd(fd[0],4,4,FL_FORMAT_RGBA8,1,FL_MAP_CPU,100,&ch)==FL_INVALID && !ch);
 CHECK(closed(fd[0]));close(fd[1]);
 pair(fd);CHECK(flCreateChannelFromSocketFd(fd[0],0,4,FL_FORMAT_RGBA8,1,FL_MAP_CPU,100,&ch)==FL_INVALID && !ch);
 CHECK(closed(fd[0]));close(fd[1]);
 pair(fd);CHECK(flOpenProducerFromSocketFd(fd[0],0,&ch)==FL_INVALID && !ch);CHECK(closed(fd[0]));close(fd[1]);
 pair(fd);CHECK(flOpenProducerFromSocketFd(fd[0],100,nullptr)==FL_INVALID);CHECK(closed(fd[0]));close(fd[1]);
 pair(fd);auto start=Clock::now();CHECK(flOpenProducerFromSocketFd(fd[0],50,&ch)==FL_DISCONNECTED && !ch);
 CHECK(Clock::now()-start<std::chrono::seconds(1));CHECK(closed(fd[0]));close(fd[1]);
 // Consumer close interrupts an unfinished 60-second native handshake immediately.
 pair(fd);CHECK(flCreateChannelFromSocketFd(fd[0],4,4,FL_FORMAT_RGBA8,1,FL_MAP_CPU,60000,&ch)==FL_OK);
 usleep(10000);start=Clock::now();flClose(ch);CHECK(Clock::now()-start<std::chrono::seconds(1));
 CHECK(closed(fd[0]));close(fd[1]);
 // An absent producer times out the asynchronous handshake without hanging acquire.
 pair(fd);CHECK(flCreateChannelFromSocketFd(fd[0],4,4,FL_FORMAT_RGBA8,1,FL_MAP_CPU,50,&ch)==FL_OK);
 flFrame frame{};CHECK(flAcquireFrame(ch,&frame,1000)==FL_DISCONNECTED);flClose(ch);close(fd[1]);
 puts("Android connected FD: rejection ownership, silent handshake and close interruption passed");
}
static void pixels(flFormat format){
 int fd[2];pair(fd);int revoke=dup(fd[0]);CHECK(revoke>=0);
 flChannel *consumer=nullptr,*producer=nullptr;
 CHECK(flCreateChannelFromSocketFd(fd[0],16,8,format,1,FL_MAP_CPU,500,&consumer)==FL_OK);
 CHECK(flOpenProducerFromSocketFd(fd[1],500,&producer)==FL_OK);
 flChannelInfo info{};CHECK(flQuery(producer,&info)==FL_OK && info.width==16 && info.height==8 && info.format==format && info.poolSize==1);
 flBuffer buffer{};CHECK(flAcquireBuffer(producer,&buffer,100)==FL_OK);
 flMappedImage mapped{};CHECK(flImageMapPlanes(buffer.image,&mapped)==FL_OK);
 for(unsigned p=0;p<mapped.planes;++p)for(unsigned y=0;y<mapped.plane[p].rows;++y)
  memset(static_cast<char*>(mapped.plane[p].pixels)+y*mapped.plane[p].stride,0x30+p,mapped.plane[p].rowBytes);
 CHECK(flSubmit(producer,&buffer,123456789)==FL_OK);
 flFrame frame{};CHECK(flAcquireFrame(consumer,&frame,500)==FL_OK && frame.ptsNs==123456789 && frame.sequence==1);
 CHECK(flImageMapPlanes(frame.image,&mapped)==FL_OK);
 for(unsigned p=0;p<mapped.planes;++p)for(unsigned y=0;y<mapped.plane[p].rows;++y)for(unsigned x=0;x<mapped.plane[p].rowBytes;++x)
  CHECK(static_cast<unsigned char*>(mapped.plane[p].pixels)[y*mapped.plane[p].stride+x]==0x30+p);
 flImageUnmap(frame.image);
 auto* retained=flImageAHardwareBuffer(frame.image);CHECK(retained);AHardwareBuffer_acquire(retained);
 CHECK(flAcquireBuffer(producer,&buffer,20)==FL_TIMEOUT);
 // Pipe is a controlled readiness surrogate, not a GPU SYNC_FD qualification.
 int fence[2];CHECK(!pipe(fence));CHECK(flReleaseSyncFd(consumer,&frame,fence[0])==FL_OK);
 CHECK(flAcquireBuffer(producer,&buffer,20)==FL_TIMEOUT);
 CHECK(flRequestGeometry(producer,32,8,format)==FL_OK);
 CHECK(flQuery(producer,&info)==FL_OK && info.width==16); // old GPU-read surrogate is still pending
 CHECK(flAcquireBuffer(producer,&buffer,20)==FL_TIMEOUT);
 CHECK(write(fence[1],"x",1)==1);CHECK(flAcquireBuffer(producer,&buffer,200)==FL_OK);
 close(fence[0]);close(fence[1]);
 CHECK(!shutdown(revoke,SHUT_RDWR));flFrame absent{};
 CHECK(flAcquireFrame(consumer,&absent,500)==FL_DISCONNECTED);
 CHECK(flAcquireBuffer(producer,&buffer,100)==FL_DISCONNECTED);
 flClose(producer);flClose(consumer);close(revoke);
 // Native references outlive channel teardown; no stale flImage pointer is used.
 AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(retained,&desc);CHECK(desc.width==16 && desc.height==8);
 if(format==FL_FORMAT_RGBA8){void* data=nullptr;CHECK(!AHardwareBuffer_lock(retained,AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&data));CHECK(*static_cast<unsigned char*>(data)==0x30);AHardwareBuffer_unlock(retained,nullptr);}
 AHardwareBuffer_release(retained);
 printf("Android connected FD: format=%u pixels/PTS, held/read-completion lifetime, revoke and retained AHB passed\n",unsigned(format));
}
static int produce(int fd){
 flChannel* producer=nullptr;CHECK(flOpenProducerFromSocketFd(fd,1000,&producer)==FL_OK);
 for(unsigned n=0;n<32;++n){
  flBuffer buffer{};CHECK(flAcquireBuffer(producer,&buffer,1000)==FL_OK);
  flMappedImage mapped{};CHECK(flImageMapPlanes(buffer.image,&mapped)==FL_OK);
  for(unsigned y=0;y<mapped.plane[0].rows;++y)memset(static_cast<char*>(mapped.plane[0].pixels)+y*mapped.plane[0].stride,n+1,mapped.plane[0].rowBytes);
  CHECK(flSubmit(producer,&buffer,int64_t(n)*1234567)==FL_OK);
 }
 flClose(producer);return 0;
}
static void processes(){
 int fd[2];pair(fd);const auto argument=std::to_string(fd[1]);
 const auto pid=fork();CHECK(pid>=0);
 if(!pid){
  close(fd[0]);if(fcntl(fd[1],F_SETFD,0)<0)_exit(98);
  execl("/proc/self/exe","fl_android_connected_fd","produce",argument.c_str(),nullptr);_exit(99);
 }
 close(fd[1]);flChannel* consumer=nullptr;
 CHECK(flCreateChannelFromSocketFd(fd[0],16,8,FL_FORMAT_RGBA8,1,FL_MAP_CPU,1000,&consumer)==FL_OK);
 for(unsigned n=0;n<32;++n){
  flFrame frame{};CHECK(flAcquireFrame(consumer,&frame,1000)==FL_OK);
  CHECK(frame.ptsNs==int64_t(n)*1234567 && frame.sequence==n+1);
  flMappedImage mapped{};CHECK(flImageMapPlanes(frame.image,&mapped)==FL_OK);
  for(unsigned y=0;y<mapped.plane[0].rows;++y)for(unsigned x=0;x<mapped.plane[0].rowBytes;++x)
   CHECK(static_cast<unsigned char*>(mapped.plane[0].pixels)[y*mapped.plane[0].stride+x]==n+1);
  flImageUnmap(frame.image);auto result=flRelease(consumer,&frame);
  CHECK(result==FL_OK || (n==31 && result==FL_DISCONNECTED));
 }
 int status=0;CHECK(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));
 flFrame absent{};CHECK(flAcquireFrame(consumer,&absent,500)==FL_DISCONNECTED);flClose(consumer);
 puts("Android connected FD: 32/32 pixel identities and exact PTS across separate processes passed");
}
int main(int argc,char** argv){
 if(argc==3 && !strcmp(argv[1],"produce"))return produce(atoi(argv[2]));
 failures();pixels(FL_FORMAT_RGBA8);pixels(FL_FORMAT_BGRA8);pixels(FL_FORMAT_NV12);processes();
 puts("Android connected FD: all cases passed (same UID; not cross-UID A/V qualification)");
}
