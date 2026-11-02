#include "fl_native_process_support.hpp"
#include <algorithm>
#include <fstream>
#include "fl_native_benchmark_gpu.hpp"
#include "framelink_trace.hpp"
#include "framelink_memory.h"
// Paced native transport workload with CPU or fence-retired GPU verification; no PCM.
static unsigned frames=3720,warmup=120,fps=60,hops=2,delayUs=0,delayFrame=180,gpu=0;
static std::string prefix;
static uint64_t nowNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static unsigned option(const char*name,unsigned fallback,unsigned lo,unsigned hi){const char*s=getenv(name);if(!s)return fallback;char*end=nullptr;auto n=strtoul(s,&end,10);CHECK(*s&&end&&!*end&&n>=lo&&n<=hi);return n;}
static void configure(){gpu=option("FL_BENCH_GPU",0,0,1);imageWidth=option("FL_BENCH_WIDTH",1920,16,3840);imageHeight=option("FL_BENCH_HEIGHT",1080,16,2160);frames=option("FL_BENCH_FRAMES",3720,2,20000);warmup=option("FL_BENCH_WARMUP",120,0,frames-2);fps=option("FL_BENCH_FPS",60,1,240);hops=option("FL_BENCH_HOPS",2,1,2);delayUs=option("FL_BENCH_DELAY_US",0,0,1000000);delayFrame=option("FL_BENCH_DELAY_FRAME",180,0,frames-1);const char*p=getenv("FL_BENCH_OUT");CHECK(p&&*p);prefix=p;alarm(frames/fps+40);}
struct Header{uint64_t identity,pts,sourceReady,magic;};
struct Observation{uint64_t identity,pts,ready,arrived,retired;};
static int producer(const std::string&name,int control){
 flChannel*p=nullptr;CHECK(flOpenProducer((name+(hops==1?".down":".up")).c_str(),&p)==FL_OK);sendByte(control,'S');receive(control,'G');
 const auto epoch=nowNs()+100000000;std::vector<std::pair<uint64_t,uint64_t>> waits;waits.reserve(frames);
 for(unsigned n=0;n<frames;n++){
  const auto due=epoch+uint64_t(n)*1000000000/fps;std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(due)));
  flBuffer b{};auto start=nowNs();CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);auto acquired=nowNs();waits.emplace_back(n,acquired-start);
  void*data=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
  for(unsigned y=0;y<imageHeight;y++)memset((char*)data+y*stride,(unsigned char)n,imageWidth*4);
  Header header{n,uint64_t(n)*1000000000/fps,nowNs(),0x46524c42454e4348ULL};memcpy(data,&header,sizeof(header));flImageUnmap(b.image);
  flFrameMetadata m{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(header.pts),uint64_t(n+1)*1000000000/fps-header.pts,77,{}};
  CHECK(flSetFrameMetadata(p,&b,&m)==FL_OK);CHECK(flSubmit(p,&b,header.pts)==FL_OK);
 }
 sendByte(control,'D');receive(control,'A');
 std::ofstream out(prefix+".producer.csv");out<<"identity,acquire_wait_ns\n";for(auto v:waits)out<<v.first<<','<<v.second<<'\n';out.close();CHECK(out.good());flClose(p);close(control);return 0;
}
static int forward(const char*exe,const std::string&name,int control,int producerControl){
 Native original[2]{};for(auto&b:original)receiveNative(control,b);flChannel*up=nullptr,*down=nullptr;CHECK(channel(name+".up",original,&up)==FL_OK);CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);
 auto child=spawn(exe,{"producer",name,"100"},{producerControl});close(producerControl);
 receive(control,'G');flsample::NativeRelay r(up,down,same);CHECK(r.initialize()==FL_OK);sendByte(control,'S');
 for(unsigned n=0;n<frames;){CHECK(r.collect()==FL_OK);flFrame f{};auto result=flAcquireFrame(up,&f,1);if(result==FL_TIMEOUT)continue;CHECK(result==FL_OK&&f.ptsNs==int64_t(uint64_t(n)*1000000000/fps));CHECK(r.forward(f)==FL_OK);++n;}
 flResult result=FL_BUSY;const auto deadline=nowNs()+5000000000ULL;while(result==FL_BUSY&&nowNs()<deadline){result=r.finish();if(result==FL_BUSY)usleep(1000);}CHECK(result==FL_OK);sendByte(control,'F');receive(control,'D');join(child);flClose(down);flClose(up);cleanup(original);close(control);return 0;
}
int main(int argc,char**argv){configure();if(argc==4&&!strcmp(argv[1],"producer"))return producer(argv[2],atoi(argv[3]));if(argc==5&&!strcmp(argv[1],"relay"))return forward(argv[0],argv[2],atoi(argv[3]),atoi(argv[4]));CHECK(argc==1);
 BenchmarkGpu gpuReader;if(gpu)gpuReader.start();std::string name="boundary-bench-"+std::to_string(getpid());Native original[2]{};allocate(original);if(gpu)gpuReader.warm(original[0]);Native reversed[2]={original[1],original[0]};flChannel*c=nullptr;CHECK(channel(name+".down",reversed,&c)==FL_OK);
 int rp[2],pp[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,rp));CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pp));
 pid_t child;if(hops==2){child=spawn(argv[0],{"relay",name,"100","101"},{rp[1],pp[1]});close(rp[1]);close(pp[1]);for(auto b:original)sendNative(rp[0],b);}else{child=spawn(argv[0],{"producer",name,"100"},{pp[1]});close(pp[1]);close(rp[1]);}
 receive(pp[0],'S');if(hops==2){sendByte(rp[0],'G');receive(rp[0],'S');}sendByte(pp[0],'G');std::vector<Observation> observations;observations.reserve(frames);
 for(unsigned n=0;n<frames;n++){
  flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);auto arrival=nowNs();if(f.ptsNs!=int64_t(uint64_t(n)*1000000000/fps))std::fprintf(stderr,"frame mismatch expected=%u actual_pts=%lld sequence=%llu ready=%llu\n",n,(long long)f.ptsNs,(unsigned long long)f.sequence,(unsigned long long)f._readyValue);CHECK(f.ptsNs==int64_t(uint64_t(n)*1000000000/fps));flFrameMetadata m{};CHECK(flGetFrameMetadata(c,&f,&m)==FL_OK&&m.dtsNs==f.ptsNs&&m.durationNs==uint64_t(n+1)*1000000000/fps-f.ptsNs&&m.streamGeneration==77);
  uint64_t lease=0;CHECK(flLeaseFrame(c,&f,&lease)==FL_OK);if(delayUs&&n==delayFrame)usleep(delayUs);
  Header h{};uint64_t retired=0;
  if(gpu)retired=gpuReader.read(c,f,lease,&h,sizeof(h),n);
  else {
   void*data=nullptr;uint64_t stride=0;CHECK(flImageMap(f.image,&data,&stride)==FL_OK);memcpy(&h,data,sizeof(h));
   for(unsigned y:{0u,imageHeight/2,imageHeight-1})for(unsigned x:{imageWidth/2,imageWidth-1})CHECK(((unsigned char*)data)[y*stride+x*4]==(unsigned char)n);
   flImageUnmap(f.image);CHECK(flCompleteReadLease(c,lease)==FL_OK);CHECK(flCollectReadLeases(c)==FL_OK);retired=nowNs();
  }
  CHECK(h.identity==n&&h.pts==uint64_t(f.ptsNs)&&h.magic==0x46524c42454e4348ULL&&arrival>=h.sourceReady);
  if(gpu&&fltrace::enabled()){
   const auto id=fltrace::id((name+".down").c_str());
   fltrace::record({"benchmark.gpu_complete",retired,0,id,f.sequence,f._readyValue,f.slot,f.ptsNs,0});
   fltrace::payload("benchmark.private_verification_readback",id,sizeof(Header)+4,FL_MEMORY_READBACK);
  }
  observations.push_back({n,h.pts,h.sourceReady,arrival,retired});
 }
 receive(pp[0],'D');if(hops==2)receive(rp[0],'F');sendByte(pp[0],'A');if(hops==2)sendByte(rp[0],'D');join(child);close(pp[0]);close(rp[0]);
 std::ofstream out(prefix+".consumer.csv");out<<"identity,pts_ns,source_marker_ns,arrival_ns,retired_ns\n";for(auto&v:observations)out<<v.identity<<','<<v.pts<<','<<v.ready<<','<<v.arrived<<','<<v.retired<<'\n';out.close();CHECK(out.good());
 auto elapsed=observations.back().arrived-observations[warmup].arrived;double rate=double(frames-warmup-1)*1e9/elapsed;
 printf("{\"hops\":%u,\"width\":%u,\"height\":%u,\"pool\":2,\"frames\":%u,\"warmup\":%u,\"fresh_fps\":%.6f,\"delay_us\":%u,\"pcm_qualified\":false,\"gpu_consumer\":%s}\n",hops,imageWidth,imageHeight,frames,warmup,rate,delayUs,gpu?"true":"false");
 flClose(c);cleanup(original);return 0;
}
