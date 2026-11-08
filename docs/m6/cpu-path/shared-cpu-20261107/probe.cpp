#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"pid=%d line%d: %s\n",getpid(),__LINE__,#x);exit(1);}}while(0)
static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static unsigned char value(unsigned n,unsigned p,unsigned y,unsigned x){return (n*17+p*59+y*7+x*3)&255;}
struct Ack{unsigned sequence;double copy_ms;};
int main(){puts("format,width,height,iteration,write_ms,read_copy_ms,verified");
for(bool nv12:{false,true})for(unsigned w:{1920u,3840u}){
 unsigned h=w==1920?1080:2160,planes=nv12?2:1,rowBytes=nv12?w:w*4,stride=(rowBytes+255)&~255u;
 size_t offset[2]={0,(size_t(stride)*h+4095)&~size_t(4095)},size=nv12?offset[1]+size_t(stride)*(h/2):size_t(stride)*h;
 int fd=memfd_create("framelink-cached-cpu-control",MFD_CLOEXEC|MFD_ALLOW_SEALING);CHECK(fd>=0);CHECK(ftruncate(fd,size)==0);CHECK(fcntl(fd,F_ADD_SEALS,F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)==0);
 auto*producer=(unsigned char*)mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(producer!=MAP_FAILED);
 int control[2];CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET,0,control)==0);pid_t child=fork();CHECK(child>=0);
 if(!child){
  close(control[0]);CHECK(munmap(producer,size)==0);
  auto*consumer=(const unsigned char*)mmap(nullptr,size,PROT_READ,MAP_SHARED,fd,0);CHECK(consumer!=MAP_FAILED);close(fd);
  std::vector<unsigned char>copy(size,0);
  for(unsigned n=0;n<8;n++){
   unsigned sequence=0;CHECK(read(control[1],&sequence,sizeof(sequence))==sizeof(sequence)&&sequence==n);
   auto start=now();for(unsigned p=0;p<planes;p++)for(unsigned y=0;y<(p?h/2:h);y++)memcpy(copy.data()+offset[p]+size_t(y)*stride,consumer+offset[p]+size_t(y)*stride,rowBytes);
   double elapsed=now()-start;
   for(unsigned p=0;p<planes;p++)for(unsigned y=0;y<(p?h/2:h);y++)for(unsigned x=0;x<rowBytes;x++)CHECK(copy[offset[p]+size_t(y)*stride+x]==value(n,p,y,x));
   Ack ack{n,elapsed};CHECK(write(control[1],&ack,sizeof(ack))==sizeof(ack));
  }
  CHECK(munmap((void*)consumer,size)==0);close(control[1]);_exit(0);
 }
 close(control[1]);std::vector<unsigned char>source(size,0);
 for(unsigned n=0;n<8;n++){
  for(unsigned p=0;p<planes;p++)for(unsigned y=0;y<(p?h/2:h);y++)for(unsigned x=0;x<rowBytes;x++)source[offset[p]+size_t(y)*stride+x]=value(n,p,y,x);
  auto start=now();for(unsigned p=0;p<planes;p++)for(unsigned y=0;y<(p?h/2:h);y++)memcpy(producer+offset[p]+size_t(y)*stride,source.data()+offset[p]+size_t(y)*stride,rowBytes);
  double elapsed=now()-start;CHECK(write(control[0],&n,sizeof(n))==sizeof(n));Ack ack{};CHECK(read(control[0],&ack,sizeof(ack))==sizeof(ack)&&ack.sequence==n);
  printf("%s,%u,%u,%u,%.6f,%.6f,yes\n",nv12?"NV12":"BGRA",w,h,n,elapsed,ack.copy_ms);fflush(stdout);
 }
 int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);close(control[0]);CHECK(munmap(producer,size)==0);close(fd);
}}
