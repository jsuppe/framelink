// Test-only authenticated FAR4 -> FAR5 native/PCM measurement harness.
// Source upload and receiver verification copy are explicit, NOT zero-copy claims.
// One source for Windows (D3D11 upload, staging copy and shared-fence
// completion), Linux (mapped DMA-BUF upload, Vulkan verification copy, then a
// completion fd) and Android (mapped AHardwareBuffer upload, mapped CPU
// verification copy, then CPU completion); everything else is common.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <dlfcn.h>
#include "framelink_ahb.h"
#else
#include "framelink_dmabuf.h"
#endif
#include "framelink_sync_fd.h"
#endif
#include <algorithm>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include "../samples/local_native_relay_setup.h"
#include "../samples/session_pcm_relay.h"
#include "../samples/session_relay_supervisor.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <set>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <type_traits>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
static void flushCsvs();
#ifdef _WIN32
#define CHECK(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);fflush(nullptr);flushCsvs();ExitProcess(1);} } while(0)
using Microsoft::WRL::ComPtr;
static const char*clockName="QPC quotient-remainder nanoseconds";
static uint64_t now(){LARGE_INTEGER t,f;CHECK(QueryPerformanceCounter(&t)&&QueryPerformanceFrequency(&f));CHECK(f.QuadPart>0&&uint64_t(f.QuadPart)<=UINT64_MAX/1000000000ULL);return uint64_t(t.QuadPart/f.QuadPart)*1000000000ULL+uint64_t(t.QuadPart%f.QuadPart)*1000000000ULL/f.QuadPart;}
#else
#define CHECK(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);fflush(nullptr);flushCsvs();_exit(1);} } while(0)
static const char*clockName="CLOCK_MONOTONIC nanoseconds";
static uint64_t now(){timespec t{};CHECK(clock_gettime(CLOCK_MONOTONIC,&t)==0);return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
static void Sleep(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
#ifndef __ANDROID__
#include "fl_bench_vulkan_readback.hpp"
#endif
#endif
namespace fs=std::filesystem;
// Pool and unread queue default to the primary profile (2 and 1); --pool and
// --video-queue select another profile, recorded in config.json.
struct Options {unsigned seconds=2,width=64,height=32,fps=30,pool=2,queue=1;bool nv12=false,rgba=false,trace=false;std::string plugin,out,role="relay";} o;
static uint64_t tick(unsigned n,unsigned rate){return uint64_t(n)*1000000000ULL/rate;}
static unsigned frames(){return o.seconds*o.fps;}
static uint64_t vpts(unsigned n){return 37000000+tick(n,o.fps);}
// Normalized pre-roll, every platform: before measurement the source
// publishes marked frames below the measured timeline (PTS under 36 ms), never
// recorded in the CSVs; the analyzer checks them separately.
//  1. PrerollSlow frames 100 ms apart, cycling through every pool slot, so
//     every process has used every pool buffer once (a first map can cost
//     more than a frame interval on its own). At that spacing no hop skips one.
//  2. Half a second at the stream's own rate, ending one interval before the
//     epoch, so measurement starts on a running stream, not an idle system
//     (on Android a task waking from idle is slow for its first frames).
// The relay forwards every pre-roll frame (END counts them); the final reader
// may skip some, but must have read every pool slot before measured frame 0.
static constexpr unsigned PrerollSlow=8,MaxPool=PrerollSlow; // the slow phase visits every slot
static unsigned poolSlots(){return o.pool;}
static constexpr uint64_t PrerollId=1ULL<<32,PrerollIntervalNs=100000000;
static unsigned prerollTotal(){return PrerollSlow+o.fps/2;}
static uint64_t prerollStep(){return 36000000/prerollTotal();}
static uint64_t prerollPts(unsigned k){return uint64_t(k)*prerollStep();}
static bool isPreroll(int64_t pts){return pts>=0&&pts<int64_t(vpts(0));}
static std::string path(const std::string& n){return (fs::path(o.out)/n).string();}
static std::string eventName(const char*n){uint64_t hash=14695981039346656037ULL;for(unsigned char c:o.out){hash^=c;hash*=1099511628211ULL;}return "FLNativeBench-"+std::to_string(hash)+"-"+n;}
#ifdef _WIN32
static void pace(uint64_t due){for(;;){auto current=now();if(current>=due)break;auto left=due-current;if(left>2000000)Sleep(DWORD(left/1000000-1));else SwitchToThread();}}
static HANDLE ev(const char* n){auto s="Local\\"+eventName(n);auto h=CreateEventA(nullptr,TRUE,FALSE,s.c_str());CHECK(h);return h;}
static void wait(HANDLE h,unsigned ms=10000){CHECK(WaitForSingleObject(h,ms)==WAIT_OBJECT_0);}
static void signalEvent(HANDLE h){CHECK(SetEvent(h));}
static void closeEvent(HANDLE h){CloseHandle(h);}
#elif defined(__ANDROID__)
static void pace(uint64_t due){for(;;){auto current=now();if(current>=due)break;auto left=due-current;
 if(left>2000000){timespec t{time_t((left-1000000)/1000000000ULL),long((left-1000000)%1000000000ULL)};nanosleep(&t,nullptr);}else sched_yield();}}
// Cross-process one-shot events. Bionic has no named POSIX semaphores, so an
// event is a marker file in the run directory: created once, waited once.
using HANDLE=const char*;
static HANDLE ev(const char* n){return n;}
static std::string eventPath(HANDLE h){return path(std::string(h)+".event");}
static void wait(HANDLE h,unsigned ms=10000){const auto deadline=now()+uint64_t(ms)*1000000ULL;struct stat st{};
 while(stat(eventPath(h).c_str(),&st)!=0){CHECK(now()<deadline);std::this_thread::sleep_for(std::chrono::microseconds(200));}}
static void signalEvent(HANDLE h){const auto t=eventPath(h)+".tmp";int fd=open(t.c_str(),O_CREAT|O_WRONLY|O_CLOEXEC,0600);CHECK(fd>=0);close(fd);CHECK(rename(t.c_str(),eventPath(h).c_str())==0);}
static void closeEvent(HANDLE){}
static void unlinkEvents(){for(auto n:{"held","release"})unlink(eventPath(n).c_str());}
#else
static void pace(uint64_t due){for(;;){auto current=now();if(current>=due)break;auto left=due-current;
 if(left>2000000){timespec t{time_t((left-1000000)/1000000000ULL),long((left-1000000)%1000000000ULL)};nanosleep(&t,nullptr);}else sched_yield();}}
// Cross-process one-shot events: POSIX named semaphores, unlinked by the relay
// (the directory owner) at the end. Each is signalled once and waited once.
using HANDLE=sem_t*;
static HANDLE ev(const char* n){auto s="/"+eventName(n);auto h=sem_open(s.c_str(),O_CREAT,0600,0);CHECK(h!=SEM_FAILED);return h;}
static void wait(HANDLE h,unsigned ms=10000){timespec t{};CHECK(clock_gettime(CLOCK_REALTIME,&t)==0);t.tv_sec+=ms/1000;t.tv_nsec+=long(ms%1000)*1000000;if(t.tv_nsec>=1000000000){++t.tv_sec;t.tv_nsec-=1000000000;}
 int r;do r=sem_timedwait(h,&t);while(r!=0&&errno==EINTR);CHECK(r==0);}
static void signalEvent(HANDLE h){CHECK(sem_post(h)==0);}
static void closeEvent(HANDLE h){sem_close(h);}
static void unlinkEvents(){for(auto n:{"held","release"})sem_unlink(("/"+eventName(n)).c_str());}
#endif
static flFormat videoFormat(){return o.nv12?FL_FORMAT_NV12:o.rgba?FL_FORMAT_RGBA8:FL_FORMAT_BGRA8;}
static const char*formatName(){return o.nv12?"NV12":o.rgba?"RGBA":"BGRA";}
static std::ofstream file(const std::string& n){std::ofstream f(path(n));CHECK(f.good());return f;}
// Per-frame and per-block CSVs. The measured threads only format a row into
// memory; a writer thread per file does the disk writes. A file write can
// stall for about 100 ms while Windows takes a shadow-copy snapshot (2026-11-23
// investigation), and that stall must not land on a timed thread. A failing
// CHECK flushes what is pending, so an abort keeps its last rows.
struct Line {std::string s;
 template<class T> Line& operator<<(const T& v){if constexpr(std::is_same_v<T,char>)s+=v;else if constexpr(std::is_convertible_v<T,const char*>)s+=static_cast<const char*>(v);else s+=std::to_string(v);return *this;}};
class Csv;
static std::mutex csvRegistry;static std::vector<Csv*> csvs;
class Csv {
 std::ofstream f;std::timed_mutex fileLock;std::mutex m;std::condition_variable cv;std::string pending;bool stop=false;std::thread writer;
 void drain(std::string&batch){f<<batch;f.flush();batch.clear();}
public:
 Csv(const std::string&name,const char*header):f(path(name)){CHECK(f.good());f<<header;
  {std::lock_guard<std::mutex> g(csvRegistry);csvs.push_back(this);}
  writer=std::thread([this]{std::string batch;for(;;){bool last;{std::unique_lock<std::mutex> l(m);cv.wait_for(l,std::chrono::milliseconds(100),[this]{return stop;});batch.swap(pending);last=stop;}
   {std::lock_guard<std::timed_mutex> g(fileLock);drain(batch);}if(last)return;}});}
 Csv(const Csv&)=delete;Csv& operator=(const Csv&)=delete;
 ~Csv(){{std::lock_guard<std::mutex> l(m);stop=true;}cv.notify_one();writer.join();{std::lock_guard<std::mutex> g(csvRegistry);csvs.erase(std::find(csvs.begin(),csvs.end(),this));}CHECK(f.good());}
 void add(const Line&row){std::lock_guard<std::mutex> l(m);pending+=row.s;}
 // Failure path only: bounded waits, never blocks the exit for long.
 void emergencyFlush(){if(!fileLock.try_lock_for(std::chrono::milliseconds(500)))return;std::string batch;{std::unique_lock<std::mutex> l(m,std::try_to_lock);if(l)batch.swap(pending);}drain(batch);fileLock.unlock();}
};
static void flushCsvs(){static std::atomic<bool> once{false};if(once.exchange(true))return;std::unique_lock<std::mutex> g(csvRegistry,std::try_to_lock);if(!g)return;for(auto*c:csvs)c->emergencyFlush();}
struct Resources {
 std::atomic<bool> done{false};std::thread worker;
#ifdef _WIN32
 Resources(){worker=std::thread([this]{auto f=file(o.role+".resources.csv");f<<"t_ns,cpu_ns,memory_kb,handles,threads\n";do{FILETIME c,e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));auto value=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof m;CHECK(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof m));DWORD h=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&h));
 // GetProcessInformation has no thread count; enumerate only this process.
 DWORD threads=0;HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);CHECK(snap!=INVALID_HANDLE_VALUE);THREADENTRY32 entry{};entry.dwSize=sizeof entry;CHECK(Thread32First(snap,&entry));do{if(entry.th32OwnerProcessID==GetCurrentProcessId())++threads;}while(Thread32Next(snap,&entry));CHECK(GetLastError()==ERROR_NO_MORE_FILES&&threads>0);CloseHandle(snap);
 f<<now()<<','<<(value(k)+value(u))*100<<','<<m.PrivateUsage/1024.0<<','<<h<<','<<threads<<'\n';f.flush();for(int i=0;i<10&&!done;++i)Sleep(100);}while(!done);});}
#else
 // Linux counterparts: user+system CPU, private resident memory
 // (smaps_rollup Private_Clean+Private_Dirty), open fds and threads.
 static uint64_t field(const char*file,const char*key){std::ifstream f(file);std::string line;uint64_t total=0;bool found=false;
  while(std::getline(f,line))if(line.rfind(key,0)==0){total+=strtoull(line.c_str()+strlen(key),nullptr,10);found=true;}CHECK(found);return total;}
 static uint64_t fds(){unsigned long n=0;DIR*d=opendir("/proc/self/fd");CHECK(d);while(auto*e=readdir(d))if(e->d_name[0]!='.')++n;closedir(d);return n-1;}
 static uint64_t cpu(){std::ifstream f("/proc/self/stat");std::string all((std::istreambuf_iterator<char>(f)),{});auto close=all.rfind(')');CHECK(close!=std::string::npos);
  std::vector<std::string> v;size_t at=close+2;while(at<all.size()){auto sp=all.find(' ',at);v.push_back(all.substr(at,sp-at));if(sp==std::string::npos)break;at=sp+1;}
  CHECK(v.size()>13);const long hz=sysconf(_SC_CLK_TCK);CHECK(hz>0);return (strtoull(v[11].c_str(),nullptr,10)+strtoull(v[12].c_str(),nullptr,10))*(1000000000ULL/uint64_t(hz));}
 Resources(){worker=std::thread([this]{auto f=file(o.role+".resources.csv");f<<"t_ns,cpu_ns,memory_kb,handles,threads\n";do{
  const uint64_t memory=field("/proc/self/smaps_rollup","Private_Clean:")+field("/proc/self/smaps_rollup","Private_Dirty:");
  const uint64_t threads=field("/proc/self/status","Threads:");CHECK(threads>0);
  f<<now()<<','<<cpu()<<','<<double(memory)<<','<<fds()<<','<<threads<<'\n';f.flush();for(int i=0;i<10&&!done;++i)Sleep(100);}while(!done);});}
#endif
 ~Resources(){done=true;worker.join();}
};
struct Stamp {uint64_t magic=0x464c4e4154495645ULL,id,pts,scheduled,source;};
// Exact check that every byte of [p, p+n) is v, vectorizable: OR of the XOR of
// each 64-bit word with the repeated pattern (a CHECK per byte cannot be
// vectorized and costs a frame interval at 1080p60 on a tablet).
static bool filledWith(const unsigned char*p,size_t n,uint8_t v){
 size_t i=0;for(;i<n&&(reinterpret_cast<uintptr_t>(p+i)&7);++i)if(p[i]!=v)return false;
 const uint64_t pattern=0x0101010101010101ULL*v;uint64_t diff=0;
 for(;i+8<=n;i+=8){uint64_t w;memcpy(&w,p+i,8);diff|=w^pattern;}
 for(;i<n;++i)diff|=uint64_t(p[i]^v);return diff==0;
}
static flSessionCapabilities caps(){flSessionCapabilities c{};c.version=2;c.videoCount=c.audioCount=1;c.video[0]={static_cast<uint32_t>(videoFormat()),o.width,o.height,o.fps,1};c.audio[0]={FL_PCM_S16,48000,2};return c;}
#ifdef _WIN32
static constexpr uint32_t nativeResource=FL_MEMORY_D3D11;
#elif defined(__ANDROID__)
static constexpr uint32_t nativeResource=FL_MEMORY_AHB;
#else
static constexpr uint32_t nativeResource=FL_MEMORY_DMABUF;
#endif
static GstFrameLinkMemoryPolicy policy(){GstFrameLinkMemoryPolicy p{};p.version=1;for(int i=0;i<2;++i){flMemoryRequirementsInit(&p.requirements[i]);p.requirements[i].allowedResourceTypes=nativeResource;p.requirements[i].allowedOperations=0;p.requirements[i].requiredUsage=FL_MEMORY_GPU_SAMPLE;p.paths[i].version=1;p.paths[i].resourceType=nativeResource;p.paths[i].supportedUsage=FL_MEMORY_GPU_SAMPLE;}return p;}
// Explicit source upload of the packed frame (BGRA or RGBA rows, or NV12 luma
// then chroma rows, width bytes each for NV12): a D3D11 UpdateSubresource, or a
// mapped DMA-BUF or AHardwareBuffer write honoring each plane's stride.
#ifndef _WIN32
static void uploadMapped(flImage*image,const std::vector<unsigned char>&pixels){
 flMappedImage m{};CHECK(flImageMapPlanes(image,&m)==FL_OK);const unsigned rowBytes=o.width*(o.nv12?1:4);size_t at=0;
 for(unsigned p=0;p<(o.nv12?2u:1u);++p){CHECK(p<m.planes);const unsigned rows=p?o.height/2:o.height;
  for(unsigned y=0;y<rows;++y,at+=rowBytes)memcpy(static_cast<unsigned char*>(m.plane[p].pixels)+size_t(y)*m.plane[p].stride,pixels.data()+at,rowBytes);}
 CHECK(at==pixels.size());flImageUnmap(image);
}
#endif
struct Audio {
 GstElement* pipe=nullptr,*port=nullptr;
 Audio(FlGstSession*s,bool producer,bool relayOutput=false){CHECK(fl_gst_session_register_track(s,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);GError*error=nullptr;pipe=gst_parse_launch(producer?"appsrc name=port is-live=true format=time block=true max-buffers=2 max-bytes=0 caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av":"framelinkaudiosrc tracks=av ! appsink name=port max-buffers=1 sync=false async=false enable-last-sample=false",&error);CHECK(pipe&&!error);port=gst_bin_get_by_name(GST_BIN(pipe),"port");CHECK(port);if(relayOutput)g_object_set(port,"block",FALSE,nullptr);auto*c=fl_gst_session_create_context(s);gst_element_set_context(pipe,c);gst_context_unref(c);CHECK(fl_gst_session_track_ready(s,FL_GST_TRACK_VIDEO,cfg.generation)==FL_GST_SESSION_READY);}
 void start(){CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);}
 void eos(){auto*b=gst_element_get_bus(pipe);auto*m=gst_bus_timed_pop_filtered(b,10*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS);gst_message_unref(m);gst_object_unref(b);}
 ~Audio(){gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(port);gst_object_unref(pipe);}
};
static void audioRow(Csv&f,const Stamp&s,uint64_t arrival,uint64_t observed=UINT64_MAX,uint64_t mapped=UINT64_MAX){Line l;l<<s.id<<','<<s.pts<<','<<s.scheduled<<','<<s.source<<','<<arrival<<",480,1920,"<<(observed==UINT64_MAX?s.pts:observed)<<','<<(mapped==UINT64_MAX?s.pts:mapped)<<'\n';f.add(l);}
static Csv audioFile(){return Csv(o.role+".audio.csv","identity,pts_ns,scheduled_ns,source_ns,arrival_ns,samples,bytes,observed_pts_ns,mapped_pts_ns\n");}
static void verifyAudio(GstBuffer*b,FlGstSession*s,unsigned n,Csv&out){const auto arrival=now();GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_READ)&&m.size==1920);Stamp stamp{};memcpy(&stamp,m.data,sizeof stamp);CHECK(stamp.magic==Stamp{}.magic&&stamp.id==n&&stamp.pts==tick(n,100));for(size_t i=sizeof stamp;i<m.size;++i)CHECK(m.data[i]==uint8_t(n+i));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);gint64 mapped=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,stamp.pts,TRUE,&mapped)==FL_GST_SESSION_READY);CHECK(GST_BUFFER_PTS(b)==uint64_t(mapped)&&GST_BUFFER_DURATION(b)==10000000);audioRow(out,stamp,arrival,GST_BUFFER_PTS(b),uint64_t(mapped));gst_buffer_unmap(b,&m);}
static void summary(uint64_t count,bool held=false){auto f=file(o.role+".summary.json");f<<"{\"video\":"<<count<<",\"audio_blocks\":"<<o.seconds*100<<",\"pcm_frames\":"<<o.seconds*48000<<",\"terminal\":true,\"held_last_proof\":"<<(held?"true":"false")<<"}\n";}
static Csv videoFile(){return Csv(o.role+".video.csv","identity,pts_ns,scheduled_ns,source_ns,arrival_ns,completed_ns,copy_bytes,sequence,slot,generation,dts_ns,duration_ns,lease,completion_value,mapped_pts_ns,acquire_begin_ns,acquired_ns,upload_begin_ns,uploaded_ns,submit_begin_ns,submitted_ns,forward_begin_ns,forwarded_ns,metadata_version,metadata_flags\n");}
struct Phases {uint64_t acquireBegin=0,acquired=0,uploadBegin=0,uploaded=0,submitBegin=0,submitted=0,forwardBegin=0,forwarded=0;};
static void row(Csv&f,const Stamp&s,uint64_t arrival,uint64_t completed,uint64_t bytes,const flFrame*frame=nullptr,const flFrameMetadata*meta=nullptr,uint64_t lease=0,uint64_t completion=0,uint64_t mapped=UINT64_MAX,Phases phases={}){Line l;l<<s.id<<','<<s.pts<<','<<s.scheduled<<','<<s.source<<','<<arrival<<','<<completed<<','<<bytes<<','<<(frame?frame->sequence:0)<<','<<(frame?frame->slot:0)<<','<<(meta?meta->streamGeneration:0)<<','<<(meta?meta->dtsNs:0)<<','<<(meta?meta->durationNs:0)<<','<<lease<<','<<completion<<','<<(mapped==UINT64_MAX?s.pts:mapped)<<','<<phases.acquireBegin<<','<<phases.acquired<<','<<phases.uploadBegin<<','<<phases.uploaded<<','<<phases.submitBegin<<','<<phases.submitted<<','<<phases.forwardBegin<<','<<phases.forwarded<<','<<(meta?meta->version:0)<<','<<(meta?meta->flags:0)<<'\n';f.add(l);}
static void source(){auto c=caps();auto p=policy();GstFrameLinkMemoryPolicy bound{};auto*s=fl_gst_session_new(3);flChannel*channel=nullptr;GError*e=nullptr;CHECK(fl_gst_session_open_local_native(s,TRUE,path("input").c_str(),&c,o.queue,o.pool,80,10000,10000,&p,&bound,&channel,nullptr,&e));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 {Audio audio(s,true);audio.start();std::vector<unsigned char> pixels(size_t(o.width)*o.height*(o.nv12?3:8)/2);auto out=videoFile();auto af=audioFile();CHECK(fl_gst_session_local_ready(s,TRUE,10000,nullptr));
#ifdef _WIN32
 ComPtr<ID3D11DeviceContext> ctx;flChannelD3D11Device(channel)->GetImmediateContext(&ctx);
 auto upload=[&](flBuffer&b){ctx->UpdateSubresource(flImageD3D11(b.image),0,nullptr,pixels.data(),o.width*(o.nv12?1:4),0);};
#else
 auto upload=[&](flBuffer&b){uploadMapped(b.image,pixels);};
#endif
 auto prerollFrame=[&](flBuffer&b,unsigned k){std::fill(pixels.begin(),pixels.end(),uint8_t(0xA5));Stamp st{Stamp{}.magic,PrerollId+k,prerollPts(k),0,0};memcpy(pixels.data(),&st,sizeof st);upload(b);
  flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(st.pts),prerollStep(),cfg.generation,{}};CHECK(flSetFrameMetadata(channel,&b,&meta)==FL_OK);CHECK(flSubmit(channel,&b,st.pts)==FL_OK);};
 // 100 ms apart, every buffer is free again, so the ring would hand back the
 // same slot each time: take another while a slot is still unused.
 {const auto start=now();std::set<uint32_t> used;for(unsigned k=0;k<PrerollSlow;++k){pace(start+k*PrerollIntervalNs);flBuffer b{};CHECK(flAcquireBuffer(channel,&b,2000)==FL_OK);
   std::vector<flBuffer> spare;while(used.count(b.slot)&&used.size()<poolSlots()){spare.push_back(b);b=flBuffer{};CHECK(flAcquireBuffer(channel,&b,2000)==FL_OK);}
   for(auto&x:spare)CHECK(flCancelBuffer(channel,&x)==FL_OK);
   used.insert(b.slot);
   prerollFrame(b,k);}
  CHECK(used.size()==poolSlots());}
 const unsigned fast=prerollTotal()-PrerollSlow;const auto fastStart=now()+tick(1,o.fps),epoch=fastStart+tick(fast,o.fps);
 std::thread pcm([&]{for(unsigned n=0;n<o.seconds*100;++n){pace(epoch+tick(n,100));auto*b=gst_buffer_new_allocate(nullptr,1920,nullptr);GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE));for(size_t i=0;i<m.size;++i)m.data[i]=uint8_t(n+i);Stamp st{Stamp{}.magic,n,tick(n,100),epoch+tick(n,100),now()};memcpy(m.data,&st,sizeof st);gst_buffer_unmap(b,&m);GST_BUFFER_PTS(b)=st.pts;GST_BUFFER_DURATION(b)=10000000;CHECK(gst_app_src_push_buffer(GST_APP_SRC(audio.port),b)==GST_FLOW_OK);audioRow(af,st,now());}CHECK(gst_app_src_end_of_stream(GST_APP_SRC(audio.port))==GST_FLOW_OK);});
 // Pre-roll at the stream rate, straight into the epoch (PCM starts there).
 for(unsigned j=0;j<fast;++j){pace(fastStart+tick(j,o.fps));flBuffer b{};CHECK(flAcquireBuffer(channel,&b,2000)==FL_OK);prerollFrame(b,PrerollSlow+j);}
 for(unsigned n=0;n<frames();++n){pace(epoch+tick(n,o.fps));flBuffer b{};Phases phases{};phases.acquireBegin=now();auto acquireResult=flAcquireBuffer(channel,&b,2000);phases.acquired=now();CHECK(acquireResult==FL_OK);std::fill(pixels.begin(),pixels.end(),uint8_t(n));Stamp st{Stamp{}.magic,n,vpts(n),epoch+tick(n,o.fps),now()};memcpy(pixels.data(),&st,sizeof st);phases.uploadBegin=now();upload(b);phases.uploaded=now();flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(st.pts),tick(n+1,o.fps)-tick(n,o.fps),cfg.generation,{}};CHECK(flSetFrameMetadata(channel,&b,&meta)==FL_OK);phases.submitBegin=now();auto submitResult=flSubmit(channel,&b,st.pts);phases.submitted=now();CHECK(submitResult==FL_OK);gint64 mappedPts=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,st.pts,FALSE,&mappedPts)==FL_GST_SESSION_READY);row(out,st,now(),0,pixels.size(),nullptr,&meta,0,0,uint64_t(mappedPts),phases);}
 pcm.join();FlGstVideoEvent end{FL_GST_VIDEO_END,prerollTotal()+frames(),vpts(frames()-1),0};CHECK(fl_gst_session_send_video_event(s,cfg.generation,&end)==FL_GST_SESSION_READY);CHECK(fl_gst_session_video_eos(s,cfg.generation,prerollTotal()+frames(),vpts(frames()-1))==FL_GST_SESSION_READY);audio.eos();CHECK(fl_gst_session_finish_local(s,TRUE,10000,nullptr));summary(frames());}
 flClose(channel);g_object_unref(s);
}
#ifdef _WIN32
static void receiver(){auto c=caps();auto p=policy();GstFrameLinkMemoryPolicy bound{};auto*s=fl_gst_session_new(3);flChannel*channel=nullptr;GError*e=nullptr;CHECK(fl_gst_session_open_local_native_pool_receiver(s,path("output").c_str(),&c,o.queue,o.pool,80,10000,10000,&p,&bound,&channel,nullptr,&e));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 {Audio audio(s,false);audio.start();auto out=videoFile();auto af=audioFile();ComPtr<ID3D11Device5> device;CHECK(SUCCEEDED(flChannelD3D11Device(channel)->QueryInterface(IID_PPV_ARGS(&device))));ComPtr<ID3D11DeviceContext> base;device->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> ctx;CHECK(SUCCEEDED(base.As(&ctx)));ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));HANDLE fh=nullptr;CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fh)));D3D11_TEXTURE2D_DESC d{};d.Width=o.width;d.Height=o.height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=o.nv12?DXGI_FORMAT_NV12:DXGI_FORMAT_B8G8R8A8_UNORM;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)));CHECK(fl_gst_session_local_ready(s,FALSE,10000,nullptr));
 std::thread pcm([&]{for(unsigned n=0;n<o.seconds*100;++n){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND);CHECK(sample);verifyAudio(gst_sample_get_buffer(sample),s,n,af);gst_sample_unref(sample);}CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(audio.port)));});
 auto held=ev("held"),release=ev("release");uint64_t fenceValue=0;std::set<uint32_t> warmed;
 // Pre-roll: verify each marked frame like a measured one (staging copy and
 // shared-fence completion), unrecorded; then require every pool slot read.
 auto warm=[&](flFrame&f){uint64_t lease=0;CHECK(flLeaseFrame(channel,&f,&lease)==FL_OK);ctx->CopyResource(staging.Get(),flImageD3D11(f.image));const uint64_t value=++fenceValue;CHECK(SUCCEEDED(ctx->Signal(fence.Get(),value)));ctx->Flush();
  CHECK(flCompleteReadLeaseD3D11Fence(channel,lease,fh,value)==FL_OK);auto deadline=now()+5000000000ULL;flResult r=FL_BUSY;while(r==FL_BUSY){CHECK(now()<deadline);r=flCollectReadLeases(channel);if(r==FL_BUSY)Sleep(1);}CHECK(r==FL_OK);
  D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));Stamp st{};memcpy(&st,map.pData,sizeof st);ctx->Unmap(staging.Get(),0);
  CHECK(st.magic==Stamp{}.magic&&st.id>=PrerollId&&st.id<PrerollId+prerollTotal()&&st.pts==uint64_t(f.ptsNs));warmed.insert(f.slot);};
 auto acquireMeasured=[&](flFrame&f){for(;;){CHECK(flAcquireFrame(channel,&f,5000)==FL_OK);if(!isPreroll(f.ptsNs))break;warm(f);}
  if(warmed.size()<poolSlots())fprintf(stderr,"pre-roll read %zu of %u pool slots before the first measured frame\n",warmed.size(),poolSlots());CHECK(warmed.size()==poolSlots());};
 for(unsigned n=0;n<frames();++n){flFrame f{};acquireMeasured(f);auto arrived=now();CHECK(f.ptsNs==int64_t(vpts(n)));flFrameMetadata m{};CHECK(flGetFrameMetadata(channel,&f,&m)==FL_OK&&m.version==FL_FRAME_METADATA_VERSION&&m.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION)&&m.streamGeneration==cfg.generation&&m.dtsNs==f.ptsNs&&m.durationNs==tick(n+1,o.fps)-tick(n,o.fps));uint64_t lease=0;CHECK(flLeaseFrame(channel,&f,&lease)==FL_OK);ctx->CopyResource(staging.Get(),flImageD3D11(f.image));const uint64_t value=++fenceValue;CHECK(SUCCEEDED(ctx->Signal(fence.Get(),value)));ctx->Flush();
 // The final control tests native ownership, never manufactures completion.
 if(n+1==frames()){CHECK(SetEvent(held));wait(release);flFrame retained{};uint64_t generation=0;CHECK(flReadLeaseFrame(channel,lease,&retained,&generation)==FL_OK&&retained.ptsNs==f.ptsNs);}
 CHECK(flCompleteReadLeaseD3D11Fence(channel,lease,fh,value)==FL_OK);auto deadline=now()+5000000000ULL;flResult r=FL_BUSY;while(r==FL_BUSY){CHECK(now()<deadline);r=flCollectReadLeases(channel);if(r==FL_BUSY)Sleep(1);}CHECK(r==FL_OK);const auto completed=now();D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));Stamp st{};memcpy(&st,map.pData,sizeof st);CHECK(st.magic==Stamp{}.magic&&st.id==n&&st.pts==vpts(n));const unsigned rowBytes=o.width*(o.nv12?1:4),rows=o.nv12?o.height*3/2:o.height;for(unsigned y=0;y<rows;++y){const size_t skip=y?0:sizeof st;CHECK(filledWith(static_cast<unsigned char*>(map.pData)+size_t(y)*map.RowPitch+skip,rowBytes-skip,uint8_t(n)));}ctx->Unmap(staging.Get(),0);gint64 mappedPts=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,st.pts,TRUE,&mappedPts)==FL_GST_SESSION_READY);row(out,st,arrived,completed,uint64_t(rowBytes)*rows,&f,&m,lease,value,uint64_t(mappedPts));}
 pcm.join();audio.eos();FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(s,cfg.generation,10000,&end)==FL_GST_SESSION_READY&&end.kind==FL_GST_VIDEO_END&&end.afterFrames==prerollTotal()+frames()&&end.ptsNs==vpts(frames()-1));guint64 count=0;gint64 last=0;CHECK(fl_gst_session_wait_video_eos(s,cfg.generation,10000,&count,&last)==FL_GST_SESSION_READY&&count==prerollTotal()+frames()&&last==int64_t(vpts(frames()-1)));CHECK(fl_gst_session_finish_local(s,FALSE,10000,nullptr));CloseHandle(held);CloseHandle(release);CloseHandle(fh);summary(count-prerollTotal(),true);}
 flClose(channel);g_object_unref(s);
}
#else
#ifdef __ANDROID__
// Android receiver verification: the held frame is read through FrameLink's
// CPU mapping (AHardwareBuffer lock; CPU_READ_OFTEN storage is cached on these
// unified-memory SoCs) into packed host memory, the counterpart of the Windows
// staging copy and the Linux Vulkan copy. The read is complete when copy()
// returns, so the lease completes on the CPU (no fence).
struct CpuReadback {
 std::vector<uint8_t> host;
 void start(flChannel*,size_t bytes){host.assign(bytes,0);}
 int copy(flChannel*,const flFrame&frame,bool nv12,uint32_t width,uint32_t height){
  flMappedImage m{};CHECK(flImageMapPlanes(frame.image,&m)==FL_OK);const unsigned rowBytes=width*(nv12?1:4);size_t at=0;
  for(unsigned p=0;p<(nv12?2u:1u);++p){CHECK(p<m.planes);const unsigned rows=p?height/2:height;
   for(unsigned y=0;y<rows;++y,at+=rowBytes)memcpy(host.data()+at,static_cast<const unsigned char*>(m.plane[p].pixels)+size_t(y)*m.plane[p].stride,rowBytes);}
  CHECK(at==host.size());flImageUnmap(frame.image);return -1;
 }
 void forget(){}
 const uint8_t* data(){return host.data();}
};
using Readback=CpuReadback;
#else
using Readback=VulkanReadback;
#endif
static void receiver(){auto c=caps();auto p=policy();GstFrameLinkMemoryPolicy bound{};auto*s=fl_gst_session_new(3);flChannel*channel=nullptr;GError*e=nullptr;CHECK(fl_gst_session_open_local_native_pool_receiver(s,path("output").c_str(),&c,o.queue,o.pool,80,10000,10000,&p,&bound,&channel,nullptr,&e));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 {Audio audio(s,false);audio.start();auto out=videoFile();auto af=audioFile();CHECK(fl_gst_session_local_ready(s,FALSE,10000,nullptr));
 std::thread pcm([&]{for(unsigned n=0;n<o.seconds*100;++n){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND);CHECK(sample);verifyAudio(gst_sample_get_buffer(sample),s,n,af);gst_sample_unref(sample);}CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(audio.port)));});
 auto held=ev("held"),release=ev("release");const unsigned rowBytes=o.width*(o.nv12?1:4),rows=o.nv12?o.height*3/2:o.height;
 Readback readback;readback.start(channel,size_t(rowBytes)*rows);uint64_t lastArrival=now();const bool diagnose=getenv("FL_BENCH_DIAG")!=nullptr;std::set<uint32_t> warmed;
 // Pre-roll: verify each marked frame like a measured one, unrecorded; then
 // require every pool slot read before measured frame 0.
 auto warm=[&](flFrame&f){uint64_t lease=0;CHECK(flLeaseFrame(channel,&f,&lease)==FL_OK);const int done=readback.copy(channel,f,o.nv12,o.width,o.height);
  CHECK((done>=0?flCompleteReadLeaseSyncFd(channel,lease,done):flCompleteReadLease(channel,lease))==FL_OK);if(done>=0)close(done);
  auto deadline=now()+5000000000ULL;flResult r=FL_BUSY;while(r==FL_BUSY){CHECK(now()<deadline);r=flCollectReadLeases(channel);if(r==FL_BUSY)Sleep(1);}CHECK(r==FL_OK);
  Stamp st{};memcpy(&st,readback.data(),sizeof st);CHECK(st.magic==Stamp{}.magic&&st.id>=PrerollId&&st.id<PrerollId+prerollTotal()&&st.pts==uint64_t(f.ptsNs));warmed.insert(f.slot);};
 auto acquireMeasured=[&](flFrame&f){for(;;){CHECK(flAcquireFrame(channel,&f,5000)==FL_OK);if(!isPreroll(f.ptsNs))break;warm(f);}
  if(warmed.size()<poolSlots())fprintf(stderr,"pre-roll read %zu of %u pool slots before the first measured frame\n",warmed.size(),poolSlots());CHECK(warmed.size()==poolSlots());};
 for(unsigned n=0;n<frames();++n){flFrame f{};acquireMeasured(f);auto arrived=now();
  if(f.ptsNs!=int64_t(vpts(n)))fprintf(stderr,"frame replaced: expected frame %u (pts %llu), received pts %lld sequence %llu slot %u, %.3f ms after the previous acquisition\n",n,(unsigned long long)vpts(n),(long long)f.ptsNs,(unsigned long long)f.sequence,f.slot,(arrived-lastArrival)/1e6);
  lastArrival=arrived;CHECK(f.ptsNs==int64_t(vpts(n)));flFrameMetadata m{};CHECK(flGetFrameMetadata(channel,&f,&m)==FL_OK&&m.version==FL_FRAME_METADATA_VERSION&&m.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION)&&m.streamGeneration==cfg.generation&&m.dtsNs==f.ptsNs&&m.durationNs==tick(n+1,o.fps)-tick(n,o.fps));uint64_t lease=0;CHECK(flLeaseFrame(channel,&f,&lease)==FL_OK);
  // Explicit GPU verification copy into host memory (the Windows staging
  // copy's counterpart); its real completion fence goes to FrameLink.
  const int done=readback.copy(channel,f,o.nv12,o.width,o.height);
  // Diagnostic mode: settle the copy while the lease is still held, and on a
  // mismatch read the same held frame through the (slow) CPU mapping.
  if(diagnose){const uint8_t*early=readback.data();Stamp e{};memcpy(&e,early,sizeof e);
   if(!(e.magic==Stamp{}.magic&&e.id==n)){flMappedImage mm{};CHECK(flImageMapPlanes(f.image,&mm)==FL_OK);Stamp cpu{};memcpy(&cpu,mm.plane[0].pixels,sizeof cpu);
    const unsigned fill=static_cast<unsigned char*>(mm.plane[0].pixels)[sizeof cpu];flImageUnmap(f.image);
    fprintf(stderr,"held frame %u: GPU copy magic=%llx id=%llu; CPU read of the same held storage magic=%llx id=%llu fill=%u\n",n,(unsigned long long)e.magic,(unsigned long long)e.id,(unsigned long long)cpu.magic,(unsigned long long)cpu.id,fill);
    const int again=readback.copy(channel,f,o.nv12,o.width,o.height);if(again>=0)close(again);const uint8_t*second=readback.data();Stamp r{};memcpy(&r,second,sizeof r);
    fprintf(stderr,"held frame %u: second GPU copy magic=%llx id=%llu\n",n,(unsigned long long)r.magic,(unsigned long long)r.id);
    readback.forget();const int fresh=readback.copy(channel,f,o.nv12,o.width,o.height);if(fresh>=0)close(fresh);const uint8_t*third=readback.data();Stamp t{};memcpy(&t,third,sizeof t);
    fprintf(stderr,"held frame %u: copy after a fresh import magic=%llx id=%llu fill=%u\n",n,(unsigned long long)t.magic,(unsigned long long)t.id,third[sizeof t]);}}
  // The final control tests native ownership, never manufactures completion.
  if(n+1==frames()){signalEvent(held);wait(release);flFrame retained{};uint64_t generation=0;CHECK(flReadLeaseFrame(channel,lease,&retained,&generation)==FL_OK&&retained.ptsNs==f.ptsNs);}
  // A copy already complete (CPU verification) completes on the CPU.
  CHECK((done>=0?flCompleteReadLeaseSyncFd(channel,lease,done):flCompleteReadLease(channel,lease))==FL_OK);if(done>=0)close(done);
  auto deadline=now()+5000000000ULL;flResult r=FL_BUSY;while(r==FL_BUSY){CHECK(now()<deadline);r=flCollectReadLeases(channel);if(r==FL_BUSY)Sleep(1);}CHECK(r==FL_OK);const auto completed=now();
  const uint8_t*copied=readback.data();Stamp st{};memcpy(&st,copied,sizeof st);
  if(!(st.magic==Stamp{}.magic&&st.id==n&&st.pts==vpts(n)))fprintf(stderr,"content mismatch: expected frame %u, copy holds magic=%llx id=%llu pts=%llu fill=%u last=%u; slot=%u sequence=%llu lease=%llu\n",n,
   (unsigned long long)st.magic,(unsigned long long)st.id,(unsigned long long)st.pts,copied[sizeof st],copied[size_t(rowBytes)*rows-1],f.slot,(unsigned long long)f.sequence,(unsigned long long)lease);
  CHECK(st.magic==Stamp{}.magic&&st.id==n&&st.pts==vpts(n));
  CHECK(filledWith(copied+sizeof st,size_t(rowBytes)*rows-sizeof st,uint8_t(n)));
  gint64 mappedPts=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,st.pts,TRUE,&mappedPts)==FL_GST_SESSION_READY);row(out,st,arrived,completed,uint64_t(rowBytes)*rows,&f,&m,lease,n+1,uint64_t(mappedPts));}
 pcm.join();audio.eos();FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(s,cfg.generation,10000,&end)==FL_GST_SESSION_READY&&end.kind==FL_GST_VIDEO_END&&end.afterFrames==prerollTotal()+frames()&&end.ptsNs==vpts(frames()-1));guint64 count=0;gint64 last=0;CHECK(fl_gst_session_wait_video_eos(s,cfg.generation,10000,&count,&last)==FL_GST_SESSION_READY&&count==prerollTotal()+frames()&&last==int64_t(vpts(frames()-1)));CHECK(fl_gst_session_finish_local(s,FALSE,10000,nullptr));closeEvent(held);closeEvent(release);summary(count-prerollTotal(),true);}
 flClose(channel);g_object_unref(s);
}
#endif
static std::string quote(const std::string&s){CHECK(s.find('"')==std::string::npos);return "\""+s+"\"";}
#ifdef _WIN32
static PROCESS_INFORMATION spawn(const char*role,HANDLE job){char exe[MAX_PATH];CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));std::string command=quote(exe)+" "+quote(o.plugin)+" --out "+quote(o.out)+" --role "+role+" --seconds "+std::to_string(o.seconds)+" --width "+std::to_string(o.width)+" --height "+std::to_string(o.height)+" --fps "+std::to_string(o.fps)+" --format "+formatName()+" --pool "+std::to_string(o.pool)+" --video-queue "+std::to_string(o.queue)+(o.trace?" --trace":"");STARTUPINFOA si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi));if(!AssignProcessToJobObject(job,pi.hProcess)){CHECK(TerminateProcess(pi.hProcess,1));wait(pi.hProcess);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CHECK(false);}if(ResumeThread(pi.hThread)==DWORD(-1)){CHECK(TerminateProcess(pi.hProcess,1));wait(pi.hProcess);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CHECK(false);}return pi;}
static void join(PROCESS_INFORMATION&p){wait(p.hProcess,15000);DWORD rc=1;CHECK(GetExitCodeProcess(p.hProcess,&rc)&&rc==0);CloseHandle(p.hThread);CloseHandle(p.hProcess);}
#else
// A child running this executable; it dies with the relay (its owner).
struct Proc{pid_t pid=-1;};
static Proc spawn(const char*role,int){const pid_t parent=getpid();
 std::vector<std::string> args={"/proc/self/exe",o.plugin,"--out",o.out,"--role",role,"--seconds",std::to_string(o.seconds),"--width",std::to_string(o.width),"--height",std::to_string(o.height),"--fps",std::to_string(o.fps),"--format",formatName(),"--pool",std::to_string(o.pool),"--video-queue",std::to_string(o.queue)};if(o.trace)args.push_back("--trace");
 Proc p;p.pid=fork();CHECK(p.pid>=0);
 if(!p.pid){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(70);std::vector<char*> argv;for(auto&a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);execv("/proc/self/exe",argv.data());_exit(72);}
 return p;}
static void join(Proc&p){int status=0;pid_t done=0;for(int i=0;i<1500&&!(done=waitpid(p.pid,&status,WNOHANG));++i)Sleep(10);CHECK(done==p.pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);p.pid=-1;}
#endif
static bool sameAllocation(flImage*a,flImage*b){
#ifdef _WIN32
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);static auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));CHECK(compare);
 return compare(flImageSharedHandle(a),flImageSharedHandle(b))!=FALSE;
#elif defined(__ANDROID__)
 using GetId=int(*)(const AHardwareBuffer*,uint64_t*);static auto getId=reinterpret_cast<GetId>(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));CHECK(getId);
 uint64_t x=0,y=0;CHECK(!getId(flImageAHardwareBuffer(a),&x)&&!getId(flImageAHardwareBuffer(b),&y)&&x&&y);return x==y;
#else
 flDmabuf x{},y{};CHECK(flImageDmabuf(a,&x)==FL_OK&&flImageDmabuf(b,&y)==FL_OK);struct stat sx{},sy{};CHECK(fstat(x.fd[0],&sx)==0&&fstat(y.fd[0],&sy)==0);
 return sx.st_dev==sy.st_dev&&sx.st_ino==sy.st_ino;
#endif
}
static void relay(){
#ifdef _WIN32
 HANDLE job=CreateJobObjectA(nullptr,nullptr);CHECK(job);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits));
#else
 const int job=0;unlinkEvents(); // no stale events from an earlier run of this directory
#endif
 auto sourceProcess=spawn("source",job),receiverProcess=spawn("receiver",job);auto c=caps();auto p=policy();auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);flsample::LocalNativeRelaySetup setup{};GError*e=nullptr;CHECK(flsample::setupLocalNativeRelay(up,down,path("input").c_str(),path("output").c_str(),&c,&p,o.queue,o.pool,80,10000,10000,nullptr,&setup,&e));
 {flsample::SessionNativeRelay video(up,down,setup.input,setup.output,sameAllocation);CHECK(video.initialize()==FL_OK);Audio in(up,false),out(down,true,true);flsample::SessionPcmRelay pcm(up,down,GST_APP_SINK(in.port),GST_APP_SRC(out.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);out.start();in.start();flsample::SessionRelaySupervisor supervisor(up,down);CHECK(supervisor.start()==FL_GST_SESSION_READY);auto vf=videoFile();auto af=audioFile();
 // Observe incoming PCM on its serialized streaming thread without stealing it.
 struct Probe{FlGstSession*s;Csv*f;unsigned n=0;} probe{up,&af};auto*pad=gst_element_get_static_pad(in.port,"sink");auto probeId=gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*,GstPadProbeInfo*i,gpointer data){auto*p=static_cast<Probe*>(data);verifyAudio(GST_PAD_PROBE_INFO_BUFFER(i),p->s,p->n++,*p->f);return GST_PAD_PROBE_OK;},&probe,nullptr);
 CHECK(fl_gst_session_local_ready(down,TRUE,10000,nullptr)&&fl_gst_session_local_ready(up,FALSE,10000,nullptr));std::thread audioWorker([&]{auto deadline=now()+uint64_t(o.seconds+20)*1000000000;while(pcm.blockCount()<o.seconds*100){CHECK(now()<deadline);auto r=pcm.forward(20);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);}});
 // Pre-roll: forwarded like any frame, unrecorded.
 for(unsigned k=0;k<prerollTotal();++k){auto deadline=now()+5000000000ULL;flFrame f{};for(;;){CHECK(now()<deadline&&supervisor.status()==FL_GST_SESSION_READY);CHECK(video.collect()==FL_OK);auto r=flAcquireFrame(setup.input,&f,1);if(r==FL_OK)break;CHECK(r==FL_TIMEOUT);}
  if(f.ptsNs!=int64_t(prerollPts(k)))fprintf(stderr,"pre-roll frame %u skipped: received pts %lld sequence %llu\n",k,(long long)f.ptsNs,(unsigned long long)f.sequence);CHECK(f.ptsNs==int64_t(prerollPts(k)));CHECK(video.forward(f)==FL_OK);}
 for(unsigned n=0;n<frames();++n){auto deadline=now()+5000000000ULL;flFrame f{};for(;;){CHECK(now()<deadline&&supervisor.status()==FL_GST_SESSION_READY);CHECK(video.collect()==FL_OK);auto r=flAcquireFrame(setup.input,&f,1);if(r==FL_OK)break;CHECK(r==FL_TIMEOUT);}auto arrival=now();CHECK(f.ptsNs==int64_t(vpts(n)));flFrameMetadata meta{};CHECK(flGetFrameMetadata(setup.input,&f,&meta)==FL_OK);CHECK(meta.version==FL_FRAME_METADATA_VERSION&&meta.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION)&&meta.dtsNs==f.ptsNs&&meta.durationNs==tick(n+1,o.fps)-tick(n,o.fps));Phases phases{};phases.forwardBegin=now();auto forwardResult=video.forward(f);phases.forwarded=now();CHECK(forwardResult==FL_OK);Stamp st{Stamp{}.magic,n,vpts(n),0,0};gint64 mappedUp=0,mappedDown=0;CHECK(fl_gst_session_map_running_time(up,meta.streamGeneration,st.pts,TRUE,&mappedUp)==FL_GST_SESSION_READY);CHECK(fl_gst_session_map_running_time(down,meta.streamGeneration,st.pts,FALSE,&mappedDown)==FL_GST_SESSION_READY&&mappedUp==mappedDown);row(vf,st,arrival,now(),0,&f,&meta,0,0,uint64_t(mappedUp),phases);}
 auto held=ev("held"),release=ev("release");wait(held);CHECK(video.collect()==FL_OK&&video.pending()>=1);CHECK(video.collect()==FL_OK&&video.pending()>=1);signalEvent(release);auto deadline=now()+10000000000ULL;while(video.pending()){CHECK(now()<deadline);CHECK(video.collect()==FL_OK);Sleep(1);}audioWorker.join();CHECK(video.forwardEvent(10000)==FL_GST_SESSION_READY&&video.videoEnded());in.eos();CHECK(pcm.blockCount()==o.seconds*100&&pcm.frameCount()==uint64_t(o.seconds)*48000&&pcm.byteCount()==uint64_t(o.seconds)*192000);CHECK(pcm.finish(video,10000)==FL_GST_SESSION_READY);out.eos();CHECK(video.finish()==FL_OK);supervisor.stop();CHECK(supervisor.status()==FL_GST_SESSION_READY);CHECK(fl_gst_session_finish_local(down,TRUE,10000,nullptr)&&fl_gst_session_finish_local(up,FALSE,10000,nullptr));gst_pad_remove_probe(pad,probeId);gst_object_unref(pad);CHECK(probe.n==o.seconds*100);closeEvent(held);closeEvent(release);summary(frames(),true);}
 join(receiverProcess);join(sourceProcess);flsample::releaseLocalPoolSnapshot(setup.snapshot);flClose(setup.output);flClose(setup.input);g_object_unref(down);g_object_unref(up);
#ifdef _WIN32
 CloseHandle(job);
#else
 unlinkEvents();
#endif
}
#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);}
#endif
int main(int argc,char**argv){CHECK(argc>=2);o.plugin=argv[1];for(int i=2;i<argc;++i){std::string k=argv[i];if(k=="--trace"){o.trace=true;continue;}CHECK(i+1<argc);std::string v=argv[++i];if(k=="--out")o.out=v;else if(k=="--role")o.role=v;else if(k=="--format"){CHECK(v=="BGRA"||v=="RGBA"||v=="NV12");o.nv12=v=="NV12";o.rgba=v=="RGBA";}else {char*end=nullptr;auto n=strtoul(v.c_str(),&end,10);CHECK(!v.empty()&&!*end&&n>0&&n<=100000);if(k=="--seconds")o.seconds=unsigned(n);else if(k=="--width")o.width=unsigned(n);else if(k=="--height")o.height=unsigned(n);else if(k=="--fps")o.fps=unsigned(n);else if(k=="--pool")o.pool=unsigned(n);else if(k=="--video-queue")o.queue=unsigned(n);else CHECK(false);}}CHECK(!o.out.empty()&&o.seconds<=1800&&(o.fps==30||o.fps==60)&&o.width>=64&&o.width<=3840&&o.height>=32&&o.height<=2160&&!(o.width%2)&&!(o.height%2)&&o.pool>=2&&o.pool<=MaxPool&&o.queue<o.pool);CHECK(o.role=="relay"||o.role=="source"||o.role=="receiver");o.out=fs::absolute(o.out).lexically_normal().string();if(o.role=="relay"){CHECK(fs::create_directory(o.out));auto f=file("config.json");f<<"{\"seconds\":"<<o.seconds<<",\"fps\":"<<o.fps<<",\"width\":"<<o.width<<",\"height\":"<<o.height<<",\"format\":\""<<formatName()<<"\",\"trace\":"<<(o.trace?"true":"false")<<",\"clock\":\""<<clockName<<"\",\"pool\":"<<o.pool<<",\"queue\":"<<o.queue<<",\"offset_ns\":37000000,\"preroll\":"<<prerollTotal()<<"}\n";}
 #ifdef _WIN32
 FILE* log=nullptr;CHECK(freopen_s(&log,path(o.role+".stderr.log").c_str(),"w",stderr)==0);
#else
 CHECK(freopen(path(o.role+".stderr.log").c_str(),"w",stderr));
#endif
 // Trace is bounded to a short run; capacity derived from expected operations.
 CHECK(!o.trace||o.seconds<=5);
#ifdef _WIN32
 _putenv_s("FRAMELINK_TRACE",o.trace?path(o.role+".trace.csv").c_str():"");_putenv_s("FRAMELINK_TRACE_EVENTS",std::to_string(o.seconds*2500+frames()*32+1024).c_str());std::thread([]{Sleep((o.seconds+60)*1000);fprintf(stderr,"owned benchmark watchdog expired\n");ExitProcess(2);}).detach();
#else
 if(o.trace)setenv("FRAMELINK_TRACE",path(o.role+".trace.csv").c_str(),1);else unsetenv("FRAMELINK_TRACE");setenv("FRAMELINK_TRACE_EVENTS",std::to_string(o.seconds*2500+frames()*32+1024).c_str(),1);std::thread([]{Sleep((o.seconds+60)*1000);fprintf(stderr,"owned benchmark watchdog expired\n");fflush(nullptr);_exit(2);}).detach();
#endif
 gst_init(&argc,&argv);
#ifdef __ANDROID__
 // GStreamer and the plugin are linked statically; argv[1] is a placeholder.
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
 GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 {Resources resources;if(o.role=="source")source();else if(o.role=="receiver")receiver();else relay();}CHECK(flTraceFlush()==FL_OK);return 0;}
#else
 GError*e=nullptr;auto*plugin=gst_plugin_load_file(o.plugin.c_str(),&e);CHECK(plugin&&!e);{Resources resources;if(o.role=="source")source();else if(o.role=="receiver")receiver();else relay();}CHECK(flTraceFlush()==FL_OK);gst_object_unref(plugin);return 0;}
#endif
