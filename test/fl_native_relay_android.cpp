#include "../samples/native_relay.h"
#include "framelink_ahb.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
static bool same(flImage*a,flImage*b){auto getId=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");uint64_t x=0,y=0;return getId&&!getId(flImageAHardwareBuffer(a),&x)&&!getId(flImageAHardwareBuffer(b),&y)&&x==y;}
int main(){if(!dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"))return 77;
 flFormat format=FL_FORMAT_RGBA8;AHardwareBuffer_Desc d{};d.width=64;d.height=32;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
 AHardwareBuffer*original[2]{};for(unsigned n=0;n<2;++n)CHECK(AHardwareBuffer_allocate(&d,&original[n])==0);
 auto name="relay-ahb-"+std::to_string(getpid());flChannel *up=nullptr,*writer=nullptr,*down=nullptr,*relayWriter=nullptr;
 AHardwareBuffer*reversed[2]={original[1],original[0]};
 CHECK(flCreateChannelFromHardwareBuffers((name+".up").c_str(),64,32,format,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,original,&up)==FL_OK);
 CHECK(flCreateChannelFromHardwareBuffers((name+".down").c_str(),64,32,format,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,reversed,&down)==FL_OK);
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
 flClose(relayWriter);flClose(writer);flClose(down);flClose(up);for(auto*b:original)AHardwareBuffer_release(b);
 puts("same AHB RGBA relay: allocation IDs, reversed slots, identity rejection, exact pixels/metadata, downstream leases prevent upstream reuse and bounded completion passed");
}
