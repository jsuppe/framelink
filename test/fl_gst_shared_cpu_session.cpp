#include "framelink_session_transport.h"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <thread>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int fds(){auto*d=opendir("/proc/self/fd");CHECK(d);int n=0;while(readdir(d))++n;closedir(d);return n;}
static flSessionConfig config(){flSessionConfig c{};c.version=2;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;c.videoTrackId=1;c.video={FL_FORMAT_BGRA8,16,16,60,1};c.videoQueueFrames=1;c.connectTimeoutMs=c.idleTimeoutMs=1000;return c;}
static void configure(FlGstSession*s,const flSessionConfig&c){flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]=c.video;CHECK(fl_gst_session_configure(s,&c,&caps,&caps)==FL_GST_SESSION_READY);}
int main(int argc,char**argv){gst_init(&argc,&argv);
 auto*warm=fl_gst_session_new(1);g_object_unref(warm);const int baseline=fds();
 flMemoryRequirements req{};CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);req.allowedResourceTypes=FL_MEMORY_SHARED_CPU;
 for(unsigned mode=0;mode<5;mode++){
  auto*s=fl_gst_session_new(1);auto*c=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);configure(c,cfg);
  flSharedCpuDescriptor d[2]{};for(unsigned i=0;i<2;i++){auto&v=d[i];v.version=1;v.resourceType=FL_MEMORY_SHARED_CPU;v.format=FL_FORMAT_BGRA8;v.width=v.height=16;v.planeCount=1;v.generation=1;v.allocationId=i+1;v.planeStride[0]=64;v.allocationBytes=1024;}
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));FlGstSessionResult sr=FL_GST_SESSION_INVALID;
  std::thread producer([&]{sr=fl_gst_session_bind_shared_cpu_fd(s,TRUE,1,fd[1],1000,&req,0,nullptr,0);});
  CHECK(fl_gst_session_bind_shared_cpu_fd(c,FALSE,1,fd[0],1000,&req,0,d,2)==FL_GST_SESSION_READY);producer.join();CHECK(sr==FL_GST_SESSION_READY);
  CHECK(!fl_gst_session_claim_shared_cpu(c,TRUE,1));CHECK(!fl_gst_session_claim_shared_cpu(c,FALSE,2));
  int duplicate[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,duplicate));CHECK(fl_gst_session_bind_shared_cpu_fd(c,FALSE,1,duplicate[0],100,&req,0,d,2)==FL_GST_SESSION_INVALID);CHECK(fcntl(duplicate[0],F_GETFD)==-1);close(duplicate[1]);
  flSessionConfig out{};CHECK(fl_gst_session_wait(c,1,1,&out)==FL_GST_SESSION_TIMEOUT); // binding is not track readiness
  auto*p=fl_gst_session_claim_shared_cpu(s,TRUE,1);CHECK(p);CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1));
  if(mode==4){auto*q=fl_gst_session_claim_shared_cpu(c,FALSE,1);CHECK(q);flSharedCpuClose(p);
   flSharedCpuLease*l=nullptr;flMappedImage v{};uint64_t seq=0;int64_t pts=0;int eos=7;
   CHECK(flSharedCpuReceiveEvent(q,&l,&v,&seq,&pts,&eos)==FL_DISCONNECTED&&eos==7);flSharedCpuClose(q);
  }else if(mode==3){fl_gst_session_cancel(c);CHECK(!fl_gst_session_claim_shared_cpu(c,FALSE,1));CHECK(flSharedCpuReceiveRelease(p)==FL_DISCONNECTED);flSharedCpuClose(p);}
  else{
   auto*q=fl_gst_session_claim_shared_cpu(c,FALSE,1);CHECK(q);
   flSharedCpuLease*w=nullptr,*r=nullptr;flMappedImage view{},read{};
   CHECK(flSharedCpuAcquireWrite(p,0,&w,&view)==FL_OK);memset(view.plane[0].pixels,73,1024);
   flFrameMetadata m{};m.version=1;m.streamGeneration=123;m.flags=FL_FRAME_HAS_DURATION;m.durationNs=100;
   CHECK(flSharedCpuSubmitWithMetadata(w,1,250,&m)==FL_OK);CHECK(flSharedCpuSendEos(p)==FL_OK);
   uint64_t seq=0;int64_t pts=0;int eos=-1;CHECK(flSharedCpuReceiveEvent(q,&r,&read,&seq,&pts,&eos)==FL_OK);CHECK(!eos&&seq==1&&pts==250);
   flFrameMetadata actual{};CHECK(flSharedCpuReadMetadata(r,&actual)==FL_OK&&actual.streamGeneration==123&&actual.durationNs==100);
   flSharedCpuLease*none=nullptr;CHECK(flSharedCpuReceiveEvent(q,&none,&view,&seq,&pts,&eos)==FL_OK&&eos&&!none);
   if(mode==1)fl_gst_session_cancel(c);
   if(mode==2){cfg.generation=2;configure(c,cfg);CHECK(!fl_gst_session_claim_shared_cpu(c,FALSE,1));}
   for(unsigned j=0;j<1024;j++)CHECK(static_cast<unsigned char*>(read.plane[0].pixels)[j]==73);
   CHECK(flSharedCpuReleaseRead(r)==(mode?FL_DISCONNECTED:FL_OK));CHECK(flSharedCpuReceiveRelease(p)==(mode?FL_DISCONNECTED:FL_OK));
   flSharedCpuClose(q);flSharedCpuClose(p);
  }
  g_object_unref(s);g_object_unref(c);CHECK(fds()==baseline);
 }
 // Cancel/reconfigure while setup is blocked: observe its outgoing hello first,
 // then interrupt. No completed old-generation pool can be claimed/published.
 for(bool replace:{false,true}){
  auto*s=fl_gst_session_new(1);auto cfg=config();configure(s,cfg);
  int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));FlGstSessionResult result=FL_GST_SESSION_READY;
  std::thread worker([&]{result=fl_gst_session_bind_shared_cpu_fd(s,TRUE,1,fd[0],60000,&req,0,nullptr,0);});
  unsigned char buf[1024];CHECK(recv(fd[1],buf,sizeof buf,0)>0);auto start=g_get_monotonic_time();
  if(replace){cfg.generation=2;configure(s,cfg);}else fl_gst_session_cancel(s);
  worker.join();CHECK(g_get_monotonic_time()-start<1000000);CHECK(result==(replace?FL_GST_SESSION_STALE:FL_GST_SESSION_CANCELLED));
  CHECK(!fl_gst_session_claim_shared_cpu(s,TRUE,1));close(fd[1]);g_object_unref(s);CHECK(fds()==baseline);
 }
 puts("CPU coordinator: exact generation/role, one claim, metadata/EOS, held mappings through cancel/replacement, blocked setup interruption and FD cleanup passed");
}
