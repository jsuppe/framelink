#include "fl_ipc.h"
#include <thread>
#include <future>
#include <chrono>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <dirent.h>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 const std::string name="ipc64-"+std::to_string(getpid());
 std::thread server([&]{auto c=fl::Channel::listen(name);CHECK(c.connected());auto m=c.recv();CHECK(m && m->fds.size()==FL_MAX_POOL_SIZE*4 && m->as<fl::RingDescMsg>());for(int fd:m->fds){CHECK(fcntl(fd,F_GETFD)&FD_CLOEXEC);close(fd);}CHECK(c.send(fl::MsgType::Bye));});
 fl::Channel client;for(int i=0;i<100&&!client.connected();++i){client=fl::Channel::connect(name,100);if(!client.connected())std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(client.connected());
 int fd=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(fd>=0);std::vector<int> fds(FL_MAX_POOL_SIZE*4,fd);fl::RingDescMsg ring{};ring.poolSize=16;
 CHECK(!client.sendWithFds(fl::MsgType::RingDesc,&ring,sizeof(ring),fds.data(),fds.size()+1));
 CHECK(!client.send(fl::MsgType::RingDesc,&ring,(std::numeric_limits<size_t>::max)()));
 CHECK(client.sendWithFds(fl::MsgType::RingDesc,&ring,sizeof(ring),fds.data(),fds.size()));close(fd);
 auto done=client.recv();CHECK(done && done->type==fl::MsgType::Bye);server.join();
 // Receiver must reject ancillary truncation and close every received FD.
 fd=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(fd>=0);
 std::promise<void> ready;auto readyFuture=ready.get_future();
 auto countFds=[](){unsigned n=0;auto* d=opendir("/proc/self/fd");CHECK(d);while(readdir(d))++n;closedir(d);return n;};
 std::thread truncServer([&]{auto c=fl::Channel::listen(name+"-trunc");CHECK(c.connected());const auto before=countFds();ready.set_value();CHECK(!c.recv());CHECK(countFds()==before);});
 fl::Channel truncClient;for(int i=0;i<100&&!truncClient.connected();++i){truncClient=fl::Channel::connect(name+"-trunc",100);if(!truncClient.connected())std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(truncClient.connected());readyFuture.wait();
 fl::MsgHeader header{};header.magic=fl::kMagic;header.type=fl::MsgType::Bye;
 iovec iov{&header,sizeof(header)};msghdr msg{};msg.msg_iov=&iov;msg.msg_iovlen=1;
 alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int)*65)]{};msg.msg_control=control;msg.msg_controllen=sizeof(control);
 auto* cm=CMSG_FIRSTHDR(&msg);cm->cmsg_level=SOL_SOCKET;cm->cmsg_type=SCM_RIGHTS;cm->cmsg_len=CMSG_LEN(sizeof(int)*65);
 int descriptors[65];for(auto& v:descriptors)v=fd;memcpy(CMSG_DATA(cm),descriptors,sizeof(descriptors));
 CHECK(sendmsg(truncClient.pollFd(),&msg,MSG_NOSIGNAL)==sizeof(header));truncServer.join();close(fd);
 // A zero-byte SEQPACKET datagram can still install SCM_RIGHTS FDs.
 // Also cover malformed nonempty headers using the same ownership check.
 for(unsigned mode=0;mode<3;++mode){
  const auto badName=name+"-bad-"+std::to_string(mode);
  int badFd=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(badFd>=0);
  std::promise<void> badReady;auto badFuture=badReady.get_future();
  std::thread badServer([&]{auto c=fl::Channel::listen(badName);CHECK(c.connected());const auto before=countFds();badReady.set_value();CHECK(!c.recv());CHECK(countFds()==before);});
  fl::Channel badClient;for(int i=0;i<100&&!badClient.connected();++i){badClient=fl::Channel::connect(badName,100);if(!badClient.connected())std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(badClient.connected());badFuture.wait();
  fl::MsgHeader badHeader{};badHeader.magic=0;badHeader.type=fl::MsgType::Bye;
  iovec badIov{&badHeader,mode==0?0:mode==1?1:sizeof(badHeader)};
  msghdr badMsg{};badMsg.msg_iov=&badIov;badMsg.msg_iovlen=1;
  alignas(cmsghdr) char badControl[CMSG_SPACE(sizeof(int))]{};badMsg.msg_control=badControl;badMsg.msg_controllen=sizeof(badControl);
  auto* badCm=CMSG_FIRSTHDR(&badMsg);badCm->cmsg_level=SOL_SOCKET;badCm->cmsg_type=SCM_RIGHTS;badCm->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(badCm),&badFd,sizeof(badFd));
  CHECK(sendmsg(badClient.pollFd(),&badMsg,MSG_NOSIGNAL)==ssize_t(badIov.iov_len));badServer.join();close(badFd);
 }
 puts("64 descriptors and expanded RingDesc pass;oversize send rejected");
}
