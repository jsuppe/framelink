#include "../samples/native_relay.h"
#include "framelink_dmabuf.h"
#include <gbm.h>
#include <drm_fourcc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool same(flImage*a,flImage*b){flDmabuf x{},y{};if(flImageDmabuf(a,&x)!=FL_OK||flImageDmabuf(b,&y)!=FL_OK||x.planes!=y.planes||x.fourcc!=y.fourcc||x.modifier!=y.modifier)return false;for(unsigned p=0;p<x.planes;++p){struct stat sx{},sy{};if(fstat(x.fd[p],&sx)||fstat(y.fd[p],&sy)||sx.st_dev!=sy.st_dev||sx.st_ino!=sy.st_ino||x.offset[p]!=y.offset[p]||x.stride[p]!=y.stride[p])return false;}return true;}
static void byte(int fd,bool send){char c=1;if(send){CHECK(write(fd,&c,1)==1);return;}pollfd poller{fd,POLLIN,0};CHECK(poll(&poller,1,5000)==1);CHECK(read(fd,&c,1)==1);}
static void processes(const flDmabuf*descriptors){
 const auto name="relay-processes-"+std::to_string(getpid());int ready[2],relayReady[2],ack[2],done[2],stop[2],connected[2];CHECK(pipe(connected)==0&&pipe(ready)==0&&pipe(relayReady)==0&&pipe(ack)==0&&pipe(done)==0&&pipe(stop)==0);
 pid_t consumer=fork();CHECK(consumer>=0);
 if(!consumer){flChannel*c=nullptr;flDmabuf reversed[2]={descriptors[1],descriptors[0]};CHECK(flCreateChannelFromDmabufs((name+".down").c_str(),64,32,FL_FORMAT_NV12,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,reversed,&c)==FL_OK);byte(ready[1],true);
  for(unsigned n=0;n<16;++n){flFrame f{};CHECK(flAcquireFrame(c,&f,3000)==FL_OK&&f.ptsNs==1000+n);flFrameMetadata m{};CHECK(flGetFrameMetadata(c,&f,&m)==FL_OK&&m.durationNs==10000000+n&&m.streamGeneration==9);flMappedImage pixels{};CHECK(flImageMapPlanes(f.image,&pixels)==FL_OK);for(unsigned p=0;p<pixels.planes;++p)for(unsigned y=0;y<pixels.plane[p].rows;++y)for(unsigned x=0;x<pixels.plane[p].rowBytes;++x)CHECK(((unsigned char*)pixels.plane[p].pixels)[y*pixels.plane[p].stride+x]==60+n+p);flImageUnmap(f.image);CHECK(flRelease(c,&f)==FL_OK);byte(ack[1],true);}
  byte(stop[0],false);flClose(c);_exit(0);
 }
 byte(ready[0],false);pid_t relayPid=fork();CHECK(relayPid>=0);
 if(!relayPid){flChannel*up=nullptr,*down=nullptr;CHECK(flCreateChannelFromDmabufs((name+".up").c_str(),64,32,FL_FORMAT_NV12,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,descriptors,&up)==FL_OK);CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);byte(relayReady[1],true);byte(connected[0],false);flsample::NativeRelay relay(up,down,same);CHECK(relay.initialize()==FL_OK);
  for(unsigned n=0;n<16;){CHECK(relay.collect()==FL_OK);flFrame f{};auto r=flAcquireFrame(up,&f,5);if(r==FL_TIMEOUT)continue;CHECK(r==FL_OK);auto forwarded=relay.forward(f);if(forwarded!=FL_OK)fprintf(stderr,"relay frame %u: %s pending=%u\n",n,flResultString(forwarded),relay.pending());CHECK(forwarded==FL_OK);++n;}
  flResult finished=FL_BUSY;for(unsigned i=0;i<3000&&finished==FL_BUSY;++i){finished=relay.finish();usleep(1000);}CHECK(finished==FL_OK&&relay.count()==16);byte(done[1],true);flClose(down);flClose(up);_exit(0);
 }
 byte(relayReady[0],false);flChannel*writer=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);byte(connected[1],true);
 for(unsigned n=0;n<16;++n){flBuffer b{};CHECK(flAcquireBuffer(writer,&b,2000)==FL_OK);flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);for(unsigned p=0;p<m.planes;++p)for(unsigned y=0;y<m.plane[p].rows;++y)memset((char*)m.plane[p].pixels+y*m.plane[p].stride,60+n+p,m.plane[p].rowBytes);flImageUnmap(b.image);flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,100+int64_t(n),10000000+n,9,{}};CHECK(flSetFrameMetadata(writer,&b,&meta)==FL_OK);CHECK(flSubmit(writer,&b,1000+n)==FL_OK);byte(ack[0],false);}
 byte(done[0],false);byte(stop[1],true);flClose(writer);int status=0;CHECK(waitpid(relayPid,&status,0)==relayPid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);CHECK(waitpid(consumer,&status,0)==consumer&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 for(int fd:{ready[0],ready[1],relayReady[0],relayReady[1],ack[0],ack[1],done[0],done[1],stop[0],stop[1],connected[0],connected[1]})close(fd);
 puts("three-process NV12 relay16/16: exact allocation matching, pixels, PTS/DTS/duration and bounded drain passed");
}
int main(){flFormat format=FL_FORMAT_NV12;uint32_t fourcc=DRM_FORMAT_NV12;
 int drm=-1;gbm_device* device=nullptr;
 for(int n=128;n<136&&!device;n++){std::string path="/dev/dri/renderD"+std::to_string(n);drm=open(path.c_str(),O_RDWR|O_CLOEXEC);if(drm>=0){device=gbm_create_device(drm);if(!device)close(drm);}}
 if(!device){puts("no GBM device");return 77;}
 gbm_bo* original[2]{};flDmabuf descriptors[2]{};
 for(int n=0;n<2;n++) {
  original[n]=gbm_bo_create(device,64,32,fourcc,GBM_BO_USE_LINEAR);CHECK(original[n]);
  auto& d=descriptors[n];d.planes=gbm_bo_get_plane_count(original[n]);d.fourcc=gbm_bo_get_format(original[n]);d.modifier=gbm_bo_get_modifier(original[n]);
  for(uint32_t p=0;p<d.planes;p++){d.fd[p]=gbm_bo_get_fd_for_plane(original[n],p);CHECK(d.fd[p]>=0);d.offset[p]=gbm_bo_get_offset(original[n],p);d.stride[p]=gbm_bo_get_stride_for_plane(original[n],p);}
 }
 processes(descriptors);
 auto name="native-relay-"+std::to_string(getpid());flChannel *up=nullptr,*writer=nullptr,*down=nullptr,*relayWriter=nullptr;
 CHECK(flCreateChannelFromDmabufs((name+".up").c_str(),64,32,format,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,descriptors,&up)==FL_OK);
 // Reverse downstream slot order: forwarding must match allocation, not index.
 flDmabuf reversed[2]={descriptors[1],descriptors[0]};
 CHECK(flCreateChannelFromDmabufs((name+".down").c_str(),64,32,format,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,reversed,&down)==FL_OK);
 CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);CHECK(flOpenProducer((name+".down").c_str(),&relayWriter)==FL_OK);
 bool allowIdentity=false;flsample::NativeRelay relay(up,relayWriter,[&](flImage*a,flImage*b){return allowIdentity&&same(a,b);});CHECK(relay.initialize()==FL_OK);
 uint64_t readers[2]{};flImage*originalImages[2]{};
 for(unsigned n=0;n<2;++n){flBuffer b{};CHECK(flAcquireBuffer(writer,&b,1000)==FL_OK);originalImages[n]=b.image;flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);for(unsigned p=0;p<m.planes;++p)for(unsigned y=0;y<m.plane[p].rows;++y)memset((char*)m.plane[p].pixels+y*m.plane[p].stride,40+n+p,m.plane[p].rowBytes);flImageUnmap(b.image);
 flFrameMetadata metadata{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,100+int64_t(n),10000000+n,7,{}};CHECK(flSetFrameMetadata(writer,&b,&metadata)==FL_OK);CHECK(flSubmit(writer,&b,1000+n)==FL_OK);
 flFrame f{};CHECK(flAcquireFrame(up,&f,1000)==FL_OK);if(n==0){CHECK(relay.forward(f)==FL_FORMAT_UNSUPPORTED&&relay.pending()==0);flFrameMetadata unchanged{};CHECK(flGetFrameMetadata(up,&f,&unchanged)==FL_OK);flFrame absent{};CHECK(flAcquireFrame(down,&absent,0)==FL_TIMEOUT);allowIdentity=true;}CHECK(relay.forward(f)==FL_OK);flFrame delivered{};CHECK(flAcquireFrame(down,&delivered,1000)==FL_OK);CHECK(same(b.image,delivered.image)&&delivered.ptsNs==1000+n);flFrameMetadata got{};CHECK(flGetFrameMetadata(down,&delivered,&got)==FL_OK&&got.dtsNs==metadata.dtsNs&&got.durationNs==metadata.durationNs&&got.streamGeneration==7);
 CHECK(flImageMapPlanes(delivered.image,&m)==FL_OK);for(unsigned p=0;p<m.planes;++p)for(unsigned y=0;y<m.plane[p].rows;++y)for(unsigned x=0;x<m.plane[p].rowBytes;++x)CHECK(((unsigned char*)m.plane[p].pixels)[y*m.plane[p].stride+x]==40+n+p);flImageUnmap(delivered.image);CHECK(flLeaseFrame(down,&delivered,&readers[n])==FL_OK);
 }
 CHECK(relay.pending()==2&&relay.count()==2);flBuffer blocked{};CHECK(flAcquireBuffer(writer,&blocked,0)==FL_TIMEOUT);CHECK(relay.finish()==FL_BUSY);
 CHECK(flCompleteReadLease(down,readers[0])==FL_OK);{auto r=flCollectReadLeases(down);CHECK(r==FL_OK||r==FL_BUSY);}
 for(unsigned n=0;n<100&&relay.pending()==2;++n){CHECK(relay.collect()==FL_OK);usleep(1000);}CHECK(relay.pending()==1);
 CHECK(flAcquireBuffer(writer,&blocked,1000)==FL_OK&&same(blocked.image,originalImages[0]));CHECK(flCancelBuffer(writer,&blocked)==FL_OK);
 CHECK(flCompleteReadLease(down,readers[1])==FL_OK);CHECK(flCollectReadLeases(down)==FL_OK);
 flResult done=FL_BUSY;for(unsigned n=0;n<100&&done==FL_BUSY;++n){done=relay.finish();usleep(1000);}CHECK(done==FL_OK);
 // A downstream disconnect is NOT proof that its read completed.
 CHECK(relay.initialize()==FL_OK);flBuffer finalBuffer{};CHECK(flAcquireBuffer(writer,&finalBuffer,1000)==FL_OK);
 flFrameMetadata finalMetadata{1,FL_FRAME_HAS_DURATION,0,20000000,7,{}};CHECK(flSetFrameMetadata(writer,&finalBuffer,&finalMetadata)==FL_OK);CHECK(flSubmit(writer,&finalBuffer,3000)==FL_OK);
 flFrame finalInput{};CHECK(flAcquireFrame(up,&finalInput,1000)==FL_OK);CHECK(relay.forward(finalInput)==FL_OK);flFrame finalOutput{};CHECK(flAcquireFrame(down,&finalOutput,1000)==FL_OK);
 uint64_t abandoned=0;CHECK(flLeaseFrame(down,&finalOutput,&abandoned)==FL_OK);flClose(down);down=nullptr;
 flResult disconnected=FL_OK;for(unsigned n=0;n<100&&disconnected==FL_OK;++n){disconnected=relay.collect();usleep(1000);}CHECK(disconnected==FL_DISCONNECTED&&relay.pending()==1);
 flBuffer spare{},unsafe{};CHECK(flAcquireBuffer(writer,&spare,1000)==FL_OK);CHECK(flAcquireBuffer(writer,&unsafe,0)==FL_TIMEOUT);CHECK(flCancelBuffer(writer,&spare)==FL_OK);
 // Entire test pipeline is now stopped, with no GPU work or future writes.
 flClose(relayWriter);flClose(writer);flClose(down);flClose(up);for(unsigned n=0;n<2;++n){for(unsigned p=0;p<descriptors[n].planes;++p)close(descriptors[n].fd[p]);gbm_bo_destroy(original[n]);}gbm_device_destroy(device);close(drm);
 puts("same-allocation NV12 relay: reversed slots, exact metadata/pixels, two downstream leases prevent upstream reuse, completion releases correct allocation; zero relay payload maps/copies");
}
