#include "av_memory_ahb.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void run(bool rgb10){flSessionConfig cfg{};cfg.version=1;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.video={uint32_t(rgb10?FL_FORMAT_RGB10A2:FL_FORMAT_RGBA8),64,32,30,1};cfg.audio={FL_PCM_S16,48000,FL_LAYOUT_STEREO};cfg.epochNs=1000000000;cfg.videoQueueFrames=2;cfg.audioQueueMs=40;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 AHardwareBuffer_Desc d{};d.width=64;d.height=32;d.layers=1;d.format=rgb10?AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
 AHardwareBuffer*images[3]{};for(auto&image:images)CHECK(AHardwareBuffer_allocate(&d,&image)==0);
 int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));flChannel*c=nullptr,*p=nullptr;CHECK(flCreateChannelFromSocketFdWithHardwareBuffers(fd[0],64,32,flFormat(cfg.video.format),2,FL_MAP_CPU,1000,images,&c)==FL_OK);CHECK(flOpenProducerFromSocketFd(fd[1],1000,&p)==FL_OK);
 std::array<unsigned char,32> owner{},imported{},other{};std::string e;CHECK(av::ahbBinding(cfg,images,2,owner,e));CHECK(av::importedAhbBinding(p,cfg,imported,e)&&owner==imported);
 AHardwareBuffer* substituted[]={images[0],images[2]};CHECK(av::ahbBinding(cfg,substituted,2,other,e)&&other!=owner);
 AHardwareBuffer*alias[]={images[0],images[0]};other.fill(0x55);auto unchanged=other;CHECK(!av::ahbBinding(cfg,alias,2,other,e)&&other==unchanged);
 auto opaque=cfg;opaque.video.format=FL_FORMAT_NV12;CHECK(!av::ahbBinding(opaque,images,2,other,e)&&other==unchanged);
 flBuffer held{};CHECK(flAcquireBuffer(p,&held,0)==FL_OK);CHECK(!av::importedAhbBinding(p,cfg,other,e)&&other==unchanged);CHECK(flCancelBuffer(p,&held)==FL_OK);CHECK(av::importedAhbBinding(p,cfg,other,e)&&other==owner);
 flClose(p);flClose(c);for(auto*image:images)AHardwareBuffer_release(image);printf("%s actual AHB allocation binding, substitution/alias/opaque-YUV and partial-reservation cleanup pass\n",rgb10?"RGB10":"RGBA");
}
int main(){run(false);run(true);}
