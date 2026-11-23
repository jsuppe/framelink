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
#include <atomic>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#define CHECK(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);fflush(nullptr);ExitProcess(1);} } while(0)
using Microsoft::WRL::ComPtr;
static const char*clockName="QPC quotient-remainder nanoseconds";
static uint64_t now(){LARGE_INTEGER t,f;CHECK(QueryPerformanceCounter(&t)&&QueryPerformanceFrequency(&f));CHECK(f.QuadPart>0&&uint64_t(f.QuadPart)<=UINT64_MAX/1000000000ULL);return uint64_t(t.QuadPart/f.QuadPart)*1000000000ULL+uint64_t(t.QuadPart%f.QuadPart)*1000000000ULL/f.QuadPart;}
#else
#define CHECK(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);fflush(nullptr);_exit(1);} } while(0)
static const char*clockName="CLOCK_MONOTONIC nanoseconds";
static uint64_t now(){timespec t{};CHECK(clock_gettime(CLOCK_MONOTONIC,&t)==0);return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
static void Sleep(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
#ifndef __ANDROID__
#include "fl_bench_vulkan_readback.hpp"
#endif
#endif
namespace fs=std::filesystem;
struct Options {unsigned seconds=2,width=64,height=32,fps=30;bool nv12=false,rgba=false,trace=false;std::string plugin,out,role="relay";} o;
static uint64_t tick(unsigned n,unsigned rate){return uint64_t(n)*1000000000ULL/rate;}
static unsigned frames(){return o.seconds*o.fps;}
static uint64_t vpts(unsigned n){return 37000000+tick(n,o.fps);}
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
static void audioRow(std::ofstream&f,const Stamp&s,uint64_t arrival,uint64_t observed=UINT64_MAX,uint64_t mapped=UINT64_MAX){f<<s.id<<','<<s.pts<<','<<s.scheduled<<','<<s.source<<','<<arrival<<",480,1920,"<<(observed==UINT64_MAX?s.pts:observed)<<','<<(mapped==UINT64_MAX?s.pts:mapped)<<'\n';}
static std::ofstream audioFile(){auto f=file(o.role+".audio.csv");f<<"identity,pts_ns,scheduled_ns,source_ns,arrival_ns,samples,bytes,observed_pts_ns,mapped_pts_ns\n";return f;}
static void verifyAudio(GstBuffer*b,FlGstSession*s,unsigned n,std::ofstream&out){const auto arrival=now();GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_READ)&&m.size==1920);Stamp stamp{};memcpy(&stamp,m.data,sizeof stamp);CHECK(stamp.magic==Stamp{}.magic&&stamp.id==n&&stamp.pts==tick(n,100));for(size_t i=sizeof stamp;i<m.size;++i)CHECK(m.data[i]==uint8_t(n+i));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);gint64 mapped=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,stamp.pts,TRUE,&mapped)==FL_GST_SESSION_READY);CHECK(GST_BUFFER_PTS(b)==uint64_t(mapped)&&GST_BUFFER_DURATION(b)==10000000);audioRow(out,stamp,arrival,GST_BUFFER_PTS(b),uint64_t(mapped));gst_buffer_unmap(b,&m);}
static void summary(uint64_t count,bool held=false){auto f=file(o.role+".summary.json");f<<"{\"video\":"<<count<<",\"audio_blocks\":"<<o.seconds*100<<",\"pcm_frames\":"<<o.seconds*48000<<",\"terminal\":true,\"held_last_proof\":"<<(held?"true":"false")<<"}\n";}
static std::ofstream videoFile(){auto f=file(o.role+".video.csv");f<<"identity,pts_ns,scheduled_ns,source_ns,arrival_ns,completed_ns,copy_bytes,sequence,slot,generation,dts_ns,duration_ns,lease,completion_value,mapped_pts_ns,acquire_begin_ns,acquired_ns,upload_begin_ns,uploaded_ns,submit_begin_ns,submitted_ns,forward_begin_ns,forwarded_ns,metadata_version,metadata_flags\n";return f;}
struct Phases {uint64_t acquireBegin=0,acquired=0,uploadBegin=0,uploaded=0,submitBegin=0,submitted=0,forwardBegin=0,forwarded=0;};
static void row(std::ofstream&f,const Stamp&s,uint64_t arrival,uint64_t completed,uint64_t bytes,const flFrame*frame=nullptr,const flFrameMetadata*meta=nullptr,uint64_t lease=0,uint64_t completion=0,uint64_t mapped=UINT64_MAX,Phases phases={}){f<<s.id<<','<<s.pts<<','<<s.scheduled<<','<<s.source<<','<<arrival<<','<<completed<<','<<bytes<<','<<(frame?frame->sequence:0)<<','<<(frame?frame->slot:0)<<','<<(meta?meta->streamGeneration:0)<<','<<(meta?meta->dtsNs:0)<<','<<(meta?meta->durationNs:0)<<','<<lease<<','<<completion<<','<<(mapped==UINT64_MAX?s.pts:mapped)<<','<<phases.acquireBegin<<','<<phases.acquired<<','<<phases.uploadBegin<<','<<phases.uploaded<<','<<phases.submitBegin<<','<<phases.submitted<<','<<phases.forwardBegin<<','<<phases.forwarded<<','<<(meta?meta->version:0)<<','<<(meta?meta->flags:0)<<'\n';}
static void source(){auto c=caps();auto p=policy();GstFrameLinkMemoryPolicy bound{};auto*s=fl_gst_session_new(3);flChannel*channel=nullptr;GError*e=nullptr;CHECK(fl_gst_session_open_local_native(s,TRUE,path("input").c_str(),&c,1,2,80,10000,10000,&p,&bound,&channel,nullptr,&e));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 {Audio audio(s,true);audio.start();std::vector<unsigned char> pixels(size_t(o.width)*o.height*(o.nv12?3:8)/2);auto out=videoFile();auto af=audioFile();CHECK(fl_gst_session_local_ready(s,TRUE,10000,nullptr));const auto epoch=now()+200000000;
 std::thread pcm([&]{for(unsigned n=0;n<o.seconds*100;++n){pace(epoch+tick(n,100));auto*b=gst_buffer_new_allocate(nullptr,1920,nullptr);GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE));for(size_t i=0;i<m.size;++i)m.data[i]=uint8_t(n+i);Stamp st{Stamp{}.magic,n,tick(n,100),epoch+tick(n,100),now()};memcpy(m.data,&st,sizeof st);gst_buffer_unmap(b,&m);GST_BUFFER_PTS(b)=st.pts;GST_BUFFER_DURATION(b)=10000000;CHECK(gst_app_src_push_buffer(GST_APP_SRC(audio.port),b)==GST_FLOW_OK);audioRow(af,st,now());}CHECK(gst_app_src_end_of_stream(GST_APP_SRC(audio.port))==GST_FLOW_OK);});
#ifdef _WIN32
 ComPtr<ID3D11DeviceContext> ctx;flChannelD3D11Device(channel)->GetImmediateContext(&ctx);
#endif
 for(unsigned n=0;n<frames();++n){pace(epoch+tick(n,o.fps));flBuffer b{};Phases phases{};phases.acquireBegin=now();auto acquireResult=flAcquireBuffer(channel,&b,2000);phases.acquired=now();CHECK(acquireResult==FL_OK);std::fill(pixels.begin(),pixels.end(),uint8_t(n));Stamp st{Stamp{}.magic,n,vpts(n),epoch+tick(n,o.fps),now()};memcpy(pixels.data(),&st,sizeof st);phases.uploadBegin=now();
#ifdef _WIN32
 ctx->UpdateSubresource(flImageD3D11(b.image),0,nullptr,pixels.data(),o.width*(o.nv12?1:4),0);
#else
 uploadMapped(b.image,pixels);
#endif
 phases.uploaded=now();flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(st.pts),tick(n+1,o.fps)-tick(n,o.fps),cfg.generation,{}};CHECK(flSetFrameMetadata(channel,&b,&meta)==FL_OK);phases.submitBegin=now();auto submitResult=flSubmit(channel,&b,st.pts);phases.submitted=now();CHECK(submitResult==FL_OK);gint64 mappedPts=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,st.pts,FALSE,&mappedPts)==FL_GST_SESSION_READY);row(out,st,now(),0,pixels.size(),nullptr,&meta,0,0,uint64_t(mappedPts),phases);}
 pcm.join();FlGstVideoEvent end{FL_GST_VIDEO_END,frames(),vpts(frames()-1),0};CHECK(fl_gst_session_send_video_event(s,cfg.generation,&end)==FL_GST_SESSION_READY);CHECK(fl_gst_session_video_eos(s,cfg.generation,frames(),vpts(frames()-1))==FL_GST_SESSION_READY);audio.eos();CHECK(fl_gst_session_finish_local(s,TRUE,10000,nullptr));summary(frames());}
 flClose(channel);g_object_unref(s);
}
#ifdef _WIN32
static void receiver(){auto c=caps();auto p=policy();GstFrameLinkMemoryPolicy bound{};auto*s=fl_gst_session_new(3);flChannel*channel=nullptr;GError*e=nullptr;CHECK(fl_gst_session_open_local_native_pool_receiver(s,path("output").c_str(),&c,1,2,80,10000,10000,&p,&bound,&channel,nullptr,&e));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 {Audio audio(s,false);audio.start();auto out=videoFile();auto af=audioFile();ComPtr<ID3D11Device5> device;CHECK(SUCCEEDED(flChannelD3D11Device(channel)->QueryInterface(IID_PPV_ARGS(&device))));ComPtr<ID3D11DeviceContext> base;device->GetImmediateContext(&base);ComPtr<ID3D11DeviceContext4> ctx;CHECK(SUCCEEDED(base.As(&ctx)));ComPtr<ID3D11Fence> fence;CHECK(SUCCEEDED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))));HANDLE fh=nullptr;CHECK(SUCCEEDED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fh)));D3D11_TEXTURE2D_DESC d{};d.Width=o.width;d.Height=o.height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=o.nv12?DXGI_FORMAT_NV12:DXGI_FORMAT_B8G8R8A8_UNORM;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)));CHECK(fl_gst_session_local_ready(s,FALSE,10000,nullptr));
 std::thread pcm([&]{for(unsigned n=0;n<o.seconds*100;++n){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND);CHECK(sample);verifyAudio(gst_sample_get_buffer(sample),s,n,af);gst_sample_unref(sample);}CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(audio.port)));});
 auto held=ev("held"),release=ev("release");for(unsigned n=0;n<frames();++n){flFrame f{};CHECK(flAcquireFrame(channel,&f,5000)==FL_OK);auto arrived=now();CHECK(f.ptsNs==int64_t(vpts(n)));flFrameMetadata m{};CHECK(flGetFrameMetadata(channel,&f,&m)==FL_OK&&m.version==FL_FRAME_METADATA_VERSION&&m.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION)&&m.streamGeneration==cfg.generation&&m.dtsNs==f.ptsNs&&m.durationNs==tick(n+1,o.fps)-tick(n,o.fps));uint64_t lease=0;CHECK(flLeaseFrame(channel,&f,&lease)==FL_OK);ctx->CopyResource(staging.Get(),flImageD3D11(f.image));CHECK(SUCCEEDED(ctx->Signal(fence.Get(),n+1)));ctx->Flush();
 // The final control tests native ownership, never manufactures completion.
 if(n+1==frames()){CHECK(SetEvent(held));wait(release);flFrame retained{};uint64_t generation=0;CHECK(flReadLeaseFrame(channel,lease,&retained,&generation)==FL_OK&&retained.ptsNs==f.ptsNs);}
 CHECK(flCompleteReadLeaseD3D11Fence(channel,lease,fh,n+1)==FL_OK);auto deadline=now()+5000000000ULL;flResult r=FL_BUSY;while(r==FL_BUSY){CHECK(now()<deadline);r=flCollectReadLeases(channel);if(r==FL_BUSY)Sleep(1);}CHECK(r==FL_OK);const auto completed=now();D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));Stamp st{};memcpy(&st,map.pData,sizeof st);CHECK(st.magic==Stamp{}.magic&&st.id==n&&st.pts==vpts(n));const unsigned rowBytes=o.width*(o.nv12?1:4),rows=o.nv12?o.height*3/2:o.height;for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<rowBytes;++x)if(y||x>=sizeof st)CHECK(static_cast<unsigned char*>(map.pData)[size_t(y)*map.RowPitch+x]==uint8_t(n));ctx->Unmap(staging.Get(),0);gint64 mappedPts=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,st.pts,TRUE,&mappedPts)==FL_GST_SESSION_READY);row(out,st,arrived,completed,uint64_t(rowBytes)*rows,&f,&m,lease,n+1,uint64_t(mappedPts));}
 pcm.join();audio.eos();FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(s,cfg.generation,10000,&end)==FL_GST_SESSION_READY&&end.kind==FL_GST_VIDEO_END&&end.afterFrames==frames()&&end.ptsNs==vpts(frames()-1));guint64 count=0;gint64 last=0;CHECK(fl_gst_session_wait_video_eos(s,cfg.generation,10000,&count,&last)==FL_GST_SESSION_READY&&count==frames()&&last==int64_t(vpts(frames()-1)));CHECK(fl_gst_session_finish_local(s,FALSE,10000,nullptr));CloseHandle(held);CloseHandle(release);CloseHandle(fh);summary(count,true);}
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
static void receiver(){auto c=caps();auto p=policy();GstFrameLinkMemoryPolicy bound{};auto*s=fl_gst_session_new(3);flChannel*channel=nullptr;GError*e=nullptr;CHECK(fl_gst_session_open_local_native_pool_receiver(s,path("output").c_str(),&c,1,2,80,10000,10000,&p,&bound,&channel,nullptr,&e));flSessionConfig cfg{};CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
 {Audio audio(s,false);audio.start();auto out=videoFile();auto af=audioFile();CHECK(fl_gst_session_local_ready(s,FALSE,10000,nullptr));
 std::thread pcm([&]{for(unsigned n=0;n<o.seconds*100;++n){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND);CHECK(sample);verifyAudio(gst_sample_get_buffer(sample),s,n,af);gst_sample_unref(sample);}CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(audio.port),10*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(audio.port)));});
 auto held=ev("held"),release=ev("release");const unsigned rowBytes=o.width*(o.nv12?1:4),rows=o.nv12?o.height*3/2:o.height;
 Readback readback;readback.start(channel,size_t(rowBytes)*rows);uint64_t lastArrival=now();const bool diagnose=getenv("FL_BENCH_DIAG")!=nullptr;
 for(unsigned n=0;n<frames();++n){flFrame f{};CHECK(flAcquireFrame(channel,&f,5000)==FL_OK);auto arrived=now();
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
  for(size_t i=sizeof st;i<size_t(rowBytes)*rows;++i)CHECK(copied[i]==uint8_t(n));
  gint64 mappedPts=0;CHECK(fl_gst_session_map_running_time(s,cfg.generation,st.pts,TRUE,&mappedPts)==FL_GST_SESSION_READY);row(out,st,arrived,completed,uint64_t(rowBytes)*rows,&f,&m,lease,n+1,uint64_t(mappedPts));}
 pcm.join();audio.eos();FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(s,cfg.generation,10000,&end)==FL_GST_SESSION_READY&&end.kind==FL_GST_VIDEO_END&&end.afterFrames==frames()&&end.ptsNs==vpts(frames()-1));guint64 count=0;gint64 last=0;CHECK(fl_gst_session_wait_video_eos(s,cfg.generation,10000,&count,&last)==FL_GST_SESSION_READY&&count==frames()&&last==int64_t(vpts(frames()-1)));CHECK(fl_gst_session_finish_local(s,FALSE,10000,nullptr));closeEvent(held);closeEvent(release);summary(count,true);}
 flClose(channel);g_object_unref(s);
}
#endif
static std::string quote(const std::string&s){CHECK(s.find('"')==std::string::npos);return "\""+s+"\"";}
#ifdef _WIN32
static PROCESS_INFORMATION spawn(const char*role,HANDLE job){char exe[MAX_PATH];CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));std::string command=quote(exe)+" "+quote(o.plugin)+" --out "+quote(o.out)+" --role "+role+" --seconds "+std::to_string(o.seconds)+" --width "+std::to_string(o.width)+" --height "+std::to_string(o.height)+" --fps "+std::to_string(o.fps)+" --format "+formatName()+(o.trace?" --trace":"");STARTUPINFOA si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi));if(!AssignProcessToJobObject(job,pi.hProcess)){CHECK(TerminateProcess(pi.hProcess,1));wait(pi.hProcess);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CHECK(false);}if(ResumeThread(pi.hThread)==DWORD(-1)){CHECK(TerminateProcess(pi.hProcess,1));wait(pi.hProcess);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CHECK(false);}return pi;}
static void join(PROCESS_INFORMATION&p){wait(p.hProcess,15000);DWORD rc=1;CHECK(GetExitCodeProcess(p.hProcess,&rc)&&rc==0);CloseHandle(p.hThread);CloseHandle(p.hProcess);}
#else
// A child running this executable; it dies with the relay (its owner).
struct Proc{pid_t pid=-1;};
static Proc spawn(const char*role,int){const pid_t parent=getpid();
 std::vector<std::string> args={"/proc/self/exe",o.plugin,"--out",o.out,"--role",role,"--seconds",std::to_string(o.seconds),"--width",std::to_string(o.width),"--height",std::to_string(o.height),"--fps",std::to_string(o.fps),"--format",formatName()};if(o.trace)args.push_back("--trace");
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
 auto sourceProcess=spawn("source",job),receiverProcess=spawn("receiver",job);auto c=caps();auto p=policy();auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);flsample::LocalNativeRelaySetup setup{};GError*e=nullptr;CHECK(flsample::setupLocalNativeRelay(up,down,path("input").c_str(),path("output").c_str(),&c,&p,1,2,80,10000,10000,nullptr,&setup,&e));
 {flsample::SessionNativeRelay video(up,down,setup.input,setup.output,sameAllocation);CHECK(video.initialize()==FL_OK);Audio in(up,false),out(down,true,true);flsample::SessionPcmRelay pcm(up,down,GST_APP_SINK(in.port),GST_APP_SRC(out.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);out.start();in.start();flsample::SessionRelaySupervisor supervisor(up,down);CHECK(supervisor.start()==FL_GST_SESSION_READY);auto vf=videoFile();auto af=audioFile();
 // Observe incoming PCM on its serialized streaming thread without stealing it.
 struct Probe{FlGstSession*s;std::ofstream*f;unsigned n=0;} probe{up,&af};auto*pad=gst_element_get_static_pad(in.port,"sink");auto probeId=gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*,GstPadProbeInfo*i,gpointer data){auto*p=static_cast<Probe*>(data);verifyAudio(GST_PAD_PROBE_INFO_BUFFER(i),p->s,p->n++,*p->f);return GST_PAD_PROBE_OK;},&probe,nullptr);
 CHECK(fl_gst_session_local_ready(down,TRUE,10000,nullptr)&&fl_gst_session_local_ready(up,FALSE,10000,nullptr));std::thread audioWorker([&]{auto deadline=now()+uint64_t(o.seconds+20)*1000000000;while(pcm.blockCount()<o.seconds*100){CHECK(now()<deadline);auto r=pcm.forward(20);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);}});
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
int main(int argc,char**argv){CHECK(argc>=2);o.plugin=argv[1];for(int i=2;i<argc;++i){std::string k=argv[i];if(k=="--trace"){o.trace=true;continue;}CHECK(i+1<argc);std::string v=argv[++i];if(k=="--out")o.out=v;else if(k=="--role")o.role=v;else if(k=="--format"){CHECK(v=="BGRA"||v=="RGBA"||v=="NV12");o.nv12=v=="NV12";o.rgba=v=="RGBA";}else {char*end=nullptr;auto n=strtoul(v.c_str(),&end,10);CHECK(!v.empty()&&!*end&&n>0&&n<=100000);if(k=="--seconds")o.seconds=unsigned(n);else if(k=="--width")o.width=unsigned(n);else if(k=="--height")o.height=unsigned(n);else if(k=="--fps")o.fps=unsigned(n);else CHECK(false);}}CHECK(!o.out.empty()&&o.seconds<=1800&&(o.fps==30||o.fps==60)&&o.width>=64&&o.width<=3840&&o.height>=32&&o.height<=2160&&!(o.width%2)&&!(o.height%2));CHECK(o.role=="relay"||o.role=="source"||o.role=="receiver");o.out=fs::absolute(o.out).lexically_normal().string();if(o.role=="relay"){CHECK(fs::create_directory(o.out));auto f=file("config.json");f<<"{\"seconds\":"<<o.seconds<<",\"fps\":"<<o.fps<<",\"width\":"<<o.width<<",\"height\":"<<o.height<<",\"format\":\""<<formatName()<<"\",\"trace\":"<<(o.trace?"true":"false")<<",\"clock\":\""<<clockName<<"\",\"pool\":2,\"queue\":1,\"offset_ns\":37000000}\n";}
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
