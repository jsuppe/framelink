#include "fl_ipc.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool closed(int fd){errno=0;return fcntl(fd,F_GETFD)==-1 && errno==EBADF;}
int main(){
 CHECK(!fl::Channel::adoptConnectedFd(-1,100).connected());
 for(unsigned mode=0;mode<5;++mode){
  int fds[2];CHECK(!socketpair(AF_UNIX,mode==0?SOCK_STREAM:SOCK_SEQPACKET,0,fds));
  const auto timeout=mode==1?0:mode==2?60001:100;
  if(mode==3){close(fds[0]);fds[0]=socket(AF_UNIX,SOCK_SEQPACKET,0);CHECK(fds[0]>=0);}
  if(mode==4){close(fds[0]);fds[0]=open("/dev/null",O_RDONLY);CHECK(fds[0]>=0);}
  CHECK(!fl::Channel::adoptConnectedFd(fds[0],timeout).connected());CHECK(closed(fds[0]));close(fds[1]);
 }
 int fds[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK,0,fds));
 int revoke=dup(fds[0]);CHECK(revoke>=0);
 {
  auto a=fl::Channel::adoptConnectedFd(fds[0],100);
  auto b=fl::Channel::adoptConnectedFd(fds[1],100);
  CHECK(a.connected()&&b.connected());CHECK(fcntl(fds[0],F_GETFD)&FD_CLOEXEC);
  CHECK(!(fcntl(fds[0],F_GETFL)&O_NONBLOCK));
  int handle=open("/dev/null",O_RDONLY);CHECK(handle>=0);fl::FrameReadyMsg frame{};frame.ptsNs=1234567;
  CHECK(a.sendWithFds(fl::MsgType::FrameReady,&frame,sizeof(frame),&handle,1));close(handle);
  auto got=b.recv();CHECK(got && got->as<fl::FrameReadyMsg>()->ptsNs==1234567 && got->fds.size()==1);
  CHECK(fcntl(got->fds[0],F_GETFD)&FD_CLOEXEC);close(got->fds[0]);
  CHECK(!shutdown(revoke,SHUT_RDWR));CHECK(!b.recv()&&!b.connected());
 }
 CHECK(closed(fds[0])&&closed(fds[1]));close(revoke);
 CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,fds));
 {
  auto a=fl::Channel::adoptConnectedFd(fds[0],50);
  auto start=std::chrono::steady_clock::now();CHECK(!a.recv());
  auto elapsed=std::chrono::steady_clock::now()-start;
  CHECK(elapsed>=std::chrono::milliseconds(25)&&elapsed<std::chrono::seconds(1));
 }
 CHECK(closed(fds[0]));close(fds[1]);
 puts("connected SEQPACKET: reject/consume, CLOEXEC/blocking, SCM_RIGHTS, revoke, bounded silent peer passed");
}
