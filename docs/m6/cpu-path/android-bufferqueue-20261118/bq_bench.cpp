// BufferQueue baseline for the FrameLink Android CPU/native receivers.
// One process: a paced producer thread writes a full RGBA frame through
// ANativeWindow_lock into an AImageReader's window; a consumer thread acquires
// every image in order. cpu: lock the AHardwareBuffer (CPU_READ_OFTEN) and copy
// into a reused buffer, then verify the frame index. gpu: take the buffer only.
// Lateness = consumer done - scheduled frame time (as fl_gst_av_bench arrival).
// Usage: bq_bench W H FPS SECONDS cpu|gpu [maxImages=2] [budget_ms]
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImageReader.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <time.h>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using Clock=std::chrono::steady_clock;
static double ms(Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();}
static double threadCpuMs(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static void stats(const char* name,std::vector<double> v){
 if(v.empty()){printf("%s=n=0\n",name);return;}
 std::sort(v.begin(),v.end());auto q=[&](double p){return v[std::min(v.size()-1,size_t(p*(v.size()-1)+0.5))];};
 printf("%s=n=%zu p50=%.2f p95=%.2f p99=%.2f p999=%.2f max=%.2f min=%.2f\n",name,v.size(),q(.5),q(.95),q(.99),q(.999),v.back(),v.front());}
int main(int argc,char**argv){
 CHECK(argc>=6);const int w=atoi(argv[1]),h=atoi(argv[2]),fps=atoi(argv[3]),secs=atoi(argv[4]);const bool cpu=!strcmp(argv[5],"cpu");
 CHECK(cpu||!strcmp(argv[5],"gpu"));const int maxImages=argc>6?atoi(argv[6]):2;const double budget=argc>7?atof(argv[7]):(fps>=60?17:34);
 const int frames=fps*secs;const auto period=std::chrono::nanoseconds(1000000000LL/fps);
 const uint64_t usage=cpu?AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN:AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
 AImageReader* reader=nullptr;CHECK(AImageReader_newWithUsage(w,h,AIMAGE_FORMAT_RGBA_8888,usage,maxImages,&reader)==AMEDIA_OK);
 ANativeWindow* win=nullptr;CHECK(AImageReader_getWindow(reader,&win)==AMEDIA_OK);
 CHECK(ANativeWindow_setBuffersGeometry(win,w,h,AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM)==0);
 std::mutex m;std::condition_variable cv;int available=0;
 struct Ctx{std::mutex*m;std::condition_variable*cv;int*available;} ctx{&m,&cv,&available};
 AImageReader_ImageListener listener{&ctx,[](void* c,AImageReader*){auto*x=(Ctx*)c;{std::lock_guard<std::mutex> l(*x->m);++*x->available;}x->cv->notify_one();}};
 CHECK(AImageReader_setImageListener(reader,&listener)==AMEDIA_OK);
 std::vector<Clock::time_point> sched(frames),posted(frames),done(frames);std::vector<double> lockWait(frames),write(frames);
 std::vector<uint8_t> src(size_t(w)*h*4);for(size_t i=0;i<src.size();++i)src[i]=uint8_t(i*7+(i/4096));
 std::atomic<int> received{0};int identityErrors=0;double prodCpu=0,consCpu=0;std::vector<uint8_t> out(cpu?size_t(w)*h*4:0);
 if(cpu)memset(out.data(),0,out.size()); // reused, pre-touched output (as framelinksrc now)
 const auto start=Clock::now()+std::chrono::milliseconds(200);
 std::thread consumer([&]{
  for(int i=0;i<frames;){
   {std::unique_lock<std::mutex> l(m);if(!cv.wait_for(l,std::chrono::seconds(2),[&]{return available>0;})){fprintf(stderr,"consumer timeout at %d\n",i);break;}--available;}
   AImage* img=nullptr;if(AImageReader_acquireNextImage(reader,&img)!=AMEDIA_OK||!img)continue;
   if(cpu){AHardwareBuffer* b=nullptr;CHECK(AImage_getHardwareBuffer(img,&b)==AMEDIA_OK&&b);AHardwareBuffer_Desc d{};AHardwareBuffer_describe(b,&d);
    void* p=nullptr;CHECK(AHardwareBuffer_lock(b,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,nullptr,&p)==0);
    for(int y=0;y<h;++y)memcpy(out.data()+size_t(y)*w*4,(uint8_t*)p+size_t(y)*d.stride*4,size_t(w)*4);
    AHardwareBuffer_unlock(b,nullptr);int idx;memcpy(&idx,out.data(),4);if(idx!=i)++identityErrors;}
   done[i]=Clock::now();AImage_delete(img);++i;received=i;}
  consCpu=threadCpuMs();});
 std::thread producer([&]{
  for(int i=0;i<frames;++i){sched[i]=start+period*i;std::this_thread::sleep_until(sched[i]);
   auto t0=Clock::now();ANativeWindow_Buffer buf;CHECK(ANativeWindow_lock(win,&buf,nullptr)==0);auto t1=Clock::now();
   for(int y=0;y<h;++y)memcpy((uint8_t*)buf.bits+size_t(y)*buf.stride*4,src.data()+size_t(y)*w*4,size_t(w)*4);
   memcpy(buf.bits,&i,4);auto t2=Clock::now();CHECK(ANativeWindow_unlockAndPost(win)==0);posted[i]=Clock::now();
   lockWait[i]=ms(t1-t0);write[i]=ms(t2-t1);}
  prodCpu=threadCpuMs();});
 producer.join();consumer.join();
 const int n=received;std::vector<double> late,lateSteady,handoff;int misses=0;
 for(int i=0;i<n;++i){double l=ms(done[i]-sched[i]);late.push_back(l);if(l>budget)++misses;if(i>=2*fps)lateSteady.push_back(l);handoff.push_back(ms(done[i]-posted[i]));}
 const double wall=ms(Clock::now()-start);
 printf("config=RGBA %dx%d@%d %ds bufferqueue %s maxImages=%d budget_ms=%.0f\n",w,h,fps,secs,cpu?"cpu-copy":"gpu-no-copy",maxImages,budget);
 printf("frames=%d/%d identity_errors=%s\n",n,frames,cpu?std::to_string(identityErrors).c_str():"not-checked");
 stats("arrival.all.ms",late);stats("arrival.steady.ms",lateSteady);stats("post_to_done.ms",handoff);
 stats("producer.lock_wait.ms",lockWait);stats("producer.write.ms",write);
 printf("deadline_misses=%d/%d\nproducer.thread_cpu.percent=%.2f\nconsumer.thread_cpu.percent=%.2f\n",misses,n,100*prodCpu/wall,100*consCpu/wall);
 AImageReader_delete(reader);return n==frames&&(!cpu||identityErrors==0)?0:1;}
