#include "av_session_provider.h"
#include "av_auth.h"
#include <thread>
#include <stdexcept>
#include <cstdio>
#include <cerrno>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
static void release(GSocketConnection* c){g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);}
static bool closed(int fd){errno=0;return fcntl(fd,F_GETFD)==-1 && errno==EBADF;}
int main(){try{
 std::string e;
 CHECK(!av::adoptConnectedStreamFd(-1,100,e));
 int pipefd[2];CHECK(!pipe(pipefd));CHECK(!av::adoptConnectedStreamFd(pipefd[0],100,e));CHECK(closed(pipefd[0]));close(pipefd[1]);
 int unconnected=socket(AF_UNIX,SOCK_STREAM,0);CHECK(unconnected>=0);CHECK(!av::adoptConnectedStreamFd(unconnected,100,e));CHECK(closed(unconnected));
 int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,fd));CHECK(!av::adoptConnectedStreamFd(fd[0],100,e));CHECK(closed(fd[0]));close(fd[1]);
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));CHECK(!av::adoptConnectedStreamFd(fd[0],0,e));CHECK(closed(fd[0]));close(fd[1]);
 // Successful byte transport, close ownership, peer death, explicit revocation.
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));int retained=dup(fd[0]);CHECK(retained>=0);
 auto* c=av::adoptConnectedStreamFd(fd[0],100,e);CHECK(c);CHECK(fcntl(fd[0],F_GETFD)&FD_CLOEXEC);
 char byte='x',got=0;CHECK(send(fd[1],&byte,1,MSG_NOSIGNAL)==1);
 CHECK(g_input_stream_read(g_io_stream_get_input_stream(G_IO_STREAM(c)),&got,1,nullptr,nullptr)==1 && got=='x');
 CHECK(!shutdown(retained,SHUT_RDWR));CHECK(recv(fd[1],&got,1,0)==0);
 release(c);CHECK(closed(fd[0]));close(retained);close(fd[1]);
 CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));c=av::adoptConnectedStreamFd(fd[0],100,e);CHECK(c);close(fd[1]);
 CHECK(g_input_stream_read(g_io_stream_get_input_stream(G_IO_STREAM(c)),&got,1,nullptr,nullptr)==0);release(c);
 // Auth is unchanged on broker-delivered endpoints. A rotated lease key rejects
 // an old-generation client's proof; role mixup also fails closed.
 for(int mode=0;mode<3;++mode){
  CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
  auto* server=av::adoptConnectedStreamFd(fd[0],100,e);auto* client=av::adoptConnectedStreamFd(fd[1],100,e);CHECK(server&&client);
  av::Endpoint current{};CHECK(av::secureRandom(current.token.data(),current.token.size(),e));auto old=current;if(mode==1)old.token[0]^=1;
  bool serverOK=false;
  std::thread t([&]{std::string err;serverOK=av::authenticateServer(server,current,'C',err);release(server);});
  bool clientOK=av::authenticateClient(client,old,mode==2?'A':'C',e);release(client);t.join();
  CHECK(serverOK==(mode==0));CHECK(clientOK==(mode==0));
 }
 puts("connected FD: ownership, stream validation, revoke/death EOF, auth, rotated key and role rejection passed");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
