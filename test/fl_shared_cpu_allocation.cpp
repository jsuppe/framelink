#include "../src/fl_shared_cpu_allocation.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <dirent.h>
#include <cstring>
#include <string>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"pid=%d line%d: %s errno=%d\n",getpid(),__LINE__,#x,errno);exit(1);}}while(0)
using Allocation=fl::SharedCpuAllocation;
static unsigned descriptors(){auto*d=opendir("/proc/self/fd");CHECK(d);unsigned n=0;while(auto*e=readdir(d))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))n++;closedir(d);return n;}
static unsigned mappings(){auto*f=fopen("/proc/self/maps","r");CHECK(f);unsigned n=0;char line[2048];while(fgets(line,sizeof(line),f))if(strstr(line,"memfd:framelink-shared-cpu"))n++;fclose(f);return n;}
static flSharedCpuDescriptor desc(bool nv12){flSharedCpuDescriptor d{};d.version=1;d.resourceType=FL_MEMORY_SHARED_CPU;d.format=nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8;d.width=130;d.height=66;d.planeCount=nv12?2:1;d.generation=7;d.allocationId=13;d.planeOffset[0]=64;d.planeStride[0]=nv12?256:768;d.allocationBytes=64+d.planeStride[0]*66;if(nv12){d.planeOffset[1]=d.allocationBytes+128;d.planeStride[1]=256;d.allocationBytes=d.planeOffset[1]+256*33;}return d;}
static void bytes(const flMappedImage&v,unsigned n,bool write){for(unsigned p=0;p<v.planes;p++)for(unsigned y=0;y<v.plane[p].rows;y++)for(unsigned x=0;x<v.plane[p].rowBytes;x++){auto*at=(unsigned char*)v.plane[p].pixels+y*v.plane[p].stride+x;unsigned char expected=(n*17+p*71+y*7+x*3)&255;if(write)*at=expected;else CHECK(*at==expected);}}
int main(){const auto baseline=descriptors(),baselineMappings=mappings();
 for(bool nv12:{false,true}){
  auto d=desc(nv12);Allocation owner;CHECK(Allocation::create(d,owner)==FL_OK);Allocation::Mapping writer;CHECK(owner.map(true,writer)==FL_OK);bytes(writer.view(),0,true);
  int ro=-1,rw=-1;CHECK(owner.exportFd(false,ro)==FL_OK&&owner.exportFd(true,rw)==FL_OK);CHECK((fcntl(ro,F_GETFL)&O_ACCMODE)==O_RDONLY);CHECK(fcntl(ro,F_GETFD)&FD_CLOEXEC);
  CHECK(ftruncate(rw,d.allocationBytes-1)<0&&errno==EPERM);CHECK(ftruncate(rw,d.allocationBytes+1)<0&&errno==EPERM);
  CHECK(mmap(nullptr,d.allocationBytes,PROT_READ|PROT_WRITE,MAP_SHARED,ro,0)==MAP_FAILED);
  Allocation reader;CHECK(Allocation::importFd(d,ro,7,13,reader)==FL_OK);
  auto*identity=reader.descriptor();CHECK(Allocation::importFd(d,ro,8,13,reader)==FL_INVALID&&reader.descriptor()==identity);CHECK(Allocation::importFd(d,ro,7,14,reader)==FL_INVALID&&reader.descriptor()==identity);
  auto wrongSize=d;wrongSize.allocationBytes+=4096;CHECK(Allocation::importFd(wrongSize,ro,7,13,reader)==FL_INVALID&&reader.descriptor()==identity);
  Allocation::Mapping held;CHECK(reader.map(false,held)==FL_OK);auto*heldPointer=held.view().plane[0].pixels;CHECK(reader.map(true,held)==FL_ACCESS_DENIED&&held.view().plane[0].pixels==heldPointer);
  int sentinel=1234;CHECK(reader.exportFd(true,sentinel)==FL_ACCESS_DENIED&&sentinel==1234);
  int mutableFd=int(syscall(SYS_memfd_create,"unsealed-negative",MFD_CLOEXEC|MFD_ALLOW_SEALING));CHECK(mutableFd>=0&&ftruncate(mutableFd,d.allocationBytes)==0);CHECK(Allocation::importFd(d,mutableFd,7,13,reader)==FL_INVALID&&reader.descriptor()==identity);CHECK(fcntl(mutableFd,F_ADD_SEALS,F_SEAL_GROW|F_SEAL_SHRINK)==0);CHECK(Allocation::importFd(d,mutableFd,7,13,reader)==FL_INVALID);
  CHECK(fcntl(mutableFd,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_SEAL)==0);Allocation frozen;CHECK(Allocation::importFd(d,mutableFd,7,13,frozen)==FL_OK);Allocation::Mapping frozenMap;auto readResult=frozen.map(false,frozenMap),writeResult=frozen.map(true,frozenMap);if(readResult!=FL_OK||writeResult!=FL_ACCESS_DENIED)fprintf(stderr,"sealed readonly map=%d write=%d seals=%x\n",readResult,writeResult,fcntl(mutableFd,F_GET_SEALS));CHECK(readResult==FL_OK&&writeResult==FL_ACCESS_DENIED);CHECK(frozen.exportFd(true,sentinel)==FL_ACCESS_DENIED&&sentinel==1234);frozenMap.reset();frozen.reset();close(mutableFd);
  int nullFd=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(nullFd>=0&&Allocation::importFd(d,nullFd,7,13,reader)==FL_INVALID);close(nullFd);
  close(ro);close(rw);owner.reset();reader.reset();bytes(writer.view(),1,true);bytes(held.view(),1,false);writer.reset();held.reset();
  // Transfer the separately encoded descriptor and one RO handle, not a native
  // image descriptor. Child drops all inherited owner mappings before import.
  CHECK(Allocation::create(d,owner)==FL_OK&&owner.map(true,writer)==FL_OK);int pair[2];CHECK(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair)==0);pid_t child=fork();CHECK(child>=0);
  if(!child){close(pair[0]);writer.reset();owner.reset();unsigned char wire[128],control[CMSG_SPACE(sizeof(int))]{};iovec iov{wire,128};msghdr msg{};msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=control;msg.msg_controllen=sizeof(control);CHECK(recvmsg(pair[1],&msg,MSG_CMSG_CLOEXEC)==128&&!(msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC)));auto*c=CMSG_FIRSTHDR(&msg);CHECK(c&&c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len==CMSG_LEN(sizeof(int)));int fd=-1;memcpy(&fd,CMSG_DATA(c),sizeof(fd));flSharedCpuDescriptor decoded{};CHECK(flSharedCpuDescriptorDecode(wire,128,&decoded)==FL_MEMORY_OK);Allocation imported;CHECK(Allocation::importFd(decoded,fd,7,13,imported)==FL_OK);close(fd);Allocation::Mapping map;CHECK(imported.map(false,map)==FL_OK);imported.reset();
   for(unsigned n=0;n<8;n++){unsigned got=99;CHECK(read(pair[1],&got,sizeof(got))==sizeof(got)&&got==n);bytes(map.view(),n,false);CHECK(write(pair[1],&got,sizeof(got))==sizeof(got));}
   map.reset();close(pair[1]);_exit(0);
  }
  close(pair[1]);CHECK(owner.exportFd(false,ro)==FL_OK);unsigned char wire[128],control[CMSG_SPACE(sizeof(int))]{};CHECK(flSharedCpuDescriptorEncode(&d,wire,128)==FL_MEMORY_OK);iovec iov{wire,128};msghdr msg{};msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=control;msg.msg_controllen=sizeof(control);auto*c=CMSG_FIRSTHDR(&msg);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),&ro,sizeof(ro));CHECK(sendmsg(pair[0],&msg,MSG_NOSIGNAL)==128);close(ro);owner.reset();
  for(unsigned n=0;n<8;n++){bytes(writer.view(),n,true);CHECK(write(pair[0],&n,sizeof(n))==sizeof(n));unsigned ack=99;CHECK(read(pair[0],&ack,sizeof(ack))==sizeof(ack)&&ack==n);}
  writer.reset();close(pair[0]);int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 for(unsigned n=0;n<100;n++){Allocation a;CHECK(Allocation::create(desc(n&1),a)==FL_OK);Allocation::Mapping map;CHECK(a.map(true,map)==FL_OK);a.reset();bytes(map.view(),n,true);map.reset();}
 CHECK(descriptors()==baseline&&mappings()==baselineMappings);puts("shared CPU: sealed imports, RO OS rights, stale/size/type negatives, retained mappings, SCM_RIGHTS pixels and 100 clean-FD/VMA cycles passed");
}
