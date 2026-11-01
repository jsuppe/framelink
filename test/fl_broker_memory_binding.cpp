#include "av_playback.h"
#include "av_memory_dmabuf.h"
#include <gbm.h>
#include <drm_fourcc.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <thread>
#include <utility>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Pair{int fd[2];Pair(){CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fd));}~Pair(){for(int f:fd)if(f>=0)close(f);}};
static void run(const char* plugin,gbm_device* device,int drm,bool nv12,int wrong){
 gbm_bo* objects[5]{};flDmabuf images[5]{};
 for(unsigned n=0;n<5;++n){objects[n]=gbm_bo_create(device,160,90,nv12?DRM_FORMAT_NV12:DRM_FORMAT_ARGB8888,GBM_BO_USE_LINEAR);CHECK(objects[n]);auto&d=images[n];d.planes=gbm_bo_get_plane_count(objects[n]);d.fourcc=gbm_bo_get_format(objects[n]);d.modifier=gbm_bo_get_modifier(objects[n]);for(unsigned p=0;p<d.planes;++p){d.fd[p]=gbm_bo_get_fd_for_plane(objects[n],p);d.offset[p]=gbm_bo_get_offset(objects[n],p);d.stride[p]=gbm_bo_get_stride_for_plane(objects[n],p);CHECK(d.fd[p]>=0);}}
 Pair control,audio;av::Endpoint ep;std::string error;CHECK(av::secureRandom(ep.token.data(),32,error));
 auto name="binding-real-"+std::to_string(getpid())+"-"+std::to_string(nv12)+std::to_string(wrong);
 auto provider=[&](bool producer){av::SessionProvider p;
  p.resolve=[&](auto&e,unsigned,auto&){e=ep;return true;};
  p.connect=[&,producer](auto&,char role,unsigned timeout,auto&e){return av::adoptConnectedStreamFd(std::exchange((role=='C'?control:audio).fd[producer?0:1],-1),timeout,e);};
  p.openVideo=[&,producer](const auto&cfg,bool role,unsigned,auto&){CHECK(role==producer);flChannel*c=nullptr;CHECK((producer?flOpenProducer(name.c_str(),&c):flCreateChannelFromDmabufs(name.c_str(),cfg.video.width,cfg.video.height,flFormat(cfg.video.format),4,FL_MAP_CPU,images,&c))==FL_OK);return c;};
  p.configureMemory=[&,producer](const auto&cfg,bool role,flChannel*c,GstElement*,av::MemoryOffer&o,unsigned,auto&e){CHECK(role==producer);flMemoryRequirementsInit(&o.requirements);o.requirements.allowedResourceTypes=FL_MEMORY_DMABUF;o.requirements.allowedOperations=FL_MEMORY_COPY;o.path.version=1;o.path.resourceType=FL_MEMORY_DMABUF;o.path.operations=FL_MEMORY_COPY;
   struct stat st{};CHECK(!fstat(drm,&st));unsigned char id[16]{};uint32_t values[]={uint32_t(major(st.st_rdev)),uint32_t(minor(st.st_rdev))};for(unsigned n=0;n<2;++n)for(unsigned b=0;b<4;++b)id[n*4+b]=uint8_t(values[n]>>(8*b));
   if(producer){
    flBuffer held{};CHECK(flAcquireBuffer(c,&held,0)==FL_OK);auto sentinel=o.resourceBinding;sentinel.fill(0x55);auto unchanged=sentinel;
    CHECK(!av::importedDmabufBinding(c,cfg,id,sentinel,e)&&sentinel==unchanged);CHECK(flCancelBuffer(c,&held)==FL_OK);
    if(wrong==2)id[0]^=1;
    return av::importedDmabufBinding(c,cfg,id,o.resourceBinding,e);
   }
   flChannelInfo info{};CHECK(flQuery(c,&info)==FL_OK);auto alternate=std::vector<flDmabuf>(images,images+4);if(wrong==1)alternate[0]=images[4];
   unsigned char unknownDevice[16]{};auto sentinel=o.resourceBinding;sentinel.fill(0x55);auto unchanged=sentinel;
   CHECK(!av::dmabufBinding(cfg,info,unknownDevice,alternate.data(),sentinel,e)&&sentinel==unchanged);
   return av::dmabufBinding(cfg,info,id,alternate.data(),o.resourceBinding,e);
  };return p;
 };
 auto server=provider(true),client=provider(false);av::ProducerOptions tx;tx.plugin=plugin;tx.frames=20;tx.sessionProvider=&server;tx.format=nv12?"NV12":"BGRA";
 av::PlaybackOptions rx;rx.plugin=plugin;rx.sessionProvider=&client;rx.format=tx.format;rx.audioSink="fakesink";rx.clockPolicy=av::PlaybackClockPolicy::ProducerMapped;rx.recoveryAttempts=0;
 std::atomic<bool> stop{false};unsigned frames=0;uint64_t pcm=0;av::PlaybackCallbacks cb;cb.onVideo=[&](auto*,auto pts,auto&){CHECK(pts==uint64_t(frames)*GST_SECOND/rx.fps);++frames;};cb.onAudio=[&](auto*b,uint64_t pts){CHECK(pts==pcm*GST_SECOND/rx.rate);pcm+=gst_buffer_get_size(b)/4;};
 bool produced=false;std::string pe,ce;std::thread thread([&]{produced=av::runBrokerProducer(tx,pe);});bool received=av::runPlayback(rx,cb,stop,ce);thread.join();
 printf("actual DMA-BUF binding %s mismatch=%d producer=%d consumer=%d frames=%u pcm=%llu errors=%s/%s\n",tx.format.c_str(),wrong,produced,received,frames,(unsigned long long)pcm,pe.c_str(),ce.c_str());CHECK(produced==!wrong&&received==!wrong);CHECK(frames==(wrong?0u:20u));CHECK(pcm==(wrong?0u:20u*48000/50));
 for(unsigned i=0;i<5;++i){for(unsigned p=0;p<images[i].planes;++p)close(images[i].fd[p]);gbm_bo_destroy(objects[i]);}
}
int main(int argc,char**argv){CHECK(argc==2);int drm=-1;gbm_device*d=nullptr;for(unsigned n=128;n<136&&!d;++n){auto p="/dev/dri/renderD"+std::to_string(n);drm=open(p.c_str(),O_RDWR|O_CLOEXEC);if(drm>=0){d=gbm_create_device(drm);if(!d)close(drm);}}CHECK(d);run(argv[1],d,drm,false,false);run(argv[1],d,drm,true,false);run(argv[1],d,drm,false,1);run(argv[1],d,drm,false,2);gbm_device_destroy(d);close(drm);}
