// M6 session benchmark: one live A/V session through the public elements or
// bins, producer and consumer in separate processes. The consumer records,
// per buffer, the arrival lateness (pipeline running time when the buffer
// leaves the FrameLink source, minus its PTS: capture-to-consumer transport)
// and the delivery lateness at appsinks (synchronized unless --transport-only); both processes sample
// CPU time, resident memory, handles and threads once a second. The driver
// prints one summary (key=value) used for the M6 baselines and budgets.
//
//   fl_gst_av_bench <plugin|static> [--seconds N] [--size WxH] [--fps F]
//     [--format RGBA|NV12|BGRA] [--mode bin|element] [--native | --shared-cpu | --gpu-readback] [--trace]
//   --gpu-readback (Linux EGL builds; M6.8c): native DMA-BUF producer into a
//   CPU receiver through GPU staging, both bins on the local native readback
//   candidate; exact BGRA/RGBA, bins, one-shot runs only.
//     [--latency-ms N] [--arrival-budget-ms N] [--transport-only]
//     [--output-queue-buffers N] (0 retains the legacy time-only limit)
//     [--no-output-preroll] (measurement appsinks async=false)
//     [--pool N] [--video-queue N] [--raw] [--out DIR] [--label NAME]
//
// M6.4 faults at rate (one-shot sessions), each gated against an outcome
// declared before the run:
//   --fault producer-kill|consumer-kill --fault-at-s T: the driver kills that
//     process T s after the producer starts (SIGKILL / TerminateProcess). The
//     survivor must end with an error (never EOS) within --fault-bound-ms
//     (default 2000) of the kill; its children must not hang.
//   --fault consumer-stall --fault-frame N --fault-ms M: the consumer's video
//     appsink blocks M ms on its Nth frame (1-based). --fault-expect, from the
//     documented overload policy (docs/GST_AV_REFERENCE.md):
//     absorbed: the session completes with every frame and arrival recovers
//       within ceil(M/period)+2 frames;
//     counted-drop (error-on-ring-full=false, the default): both sides reach
//       EOS, the producer sink drops at least one frame as ring-full, and
//       delivered + dropped == pushed exactly;
//     replacement-failure: the consumer fails with the coordinated "replaced
//       before it was read" error (a receiver that releases slots as it copies
//       out lets the producer overwrite the unread queue) and the producer does
//       not report EOS;
//     explicit-failure (with --error-on-ring-full): the producer fails with
//       the pool-exhausted error, the consumer does not report EOS, and both
//       sides end within the run limit.
//   Any loss not matched by the producer's drop count fails every expectation.
// Producer faults: --fault producer-burst|audio-stall|audio-drop|audio-gap
//   --fault-frame N (video frame, or 10 ms audio block, 1-based) --fault-ms M
//   (1..1000), injected at the source's push (see pushed()). Expectations:
//     producer-burst: absorbed (as above, on video) or counted-drop;
//     audio-stall: absorbed (on audio; recovery within ceil(M/10)+2 blocks);
//     audio-drop: explicit-failure (the producer refuses the timestamp gap,
//       "continuity mismatch"; the consumer does not report EOS and ends within
//       --fault-bound-ms of the first missing block);
//     audio-gap: gap-visible (both EOS, video complete, exactly the dropped
//       samples missing, one consumer PTS jump and one GAP event, each of the
//       dropped duration within 2 ns).
// Diagnostic fault: --video-stall-stage source|post-push
//   --video-stall-frame N (1-based) --video-stall-ms N (1..1000).
// CPU source-thread delay only; not GPU injection or qualification.
// Resource isolation: --role consumer --local-consumer --result FILE
// feeds the usual queues/appsinks directly; never transport qualification.
// Software timing on the pipeline clock; physical display/speaker output is
// not measured. A/V offset is the difference between the two tracks' render
// lateness (same-second windows), i.e. sink scheduling, given that the session
// preserves relative PTS exactly (M5b timeline tests).
#include "fl_gst_av_harness.h"
#include <glib/gstdio.h>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#ifdef _WIN32
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <signal.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef FL_BENCH_GPU_READBACK
#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <linux/dma-buf.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif

struct Options {guint stallFrame=0,stallMs=0;std::string stallStage;std::string fault,faultExpect;guint faultAtS=0,faultFrame=0,faultMs=0,faultBoundMs=2000;guint seconds=60,cycles=0;std::string mode="bin",label="run",out=".",restart="producer";bool native=false,trace=false,raw=false,sourceOnly=false,localConsumer=false;};
static guint gOutputQueueBuffers=0;
static GstClockTime gArrivalBudget=LATENCY;
// Producer-side processes (roles producer, lifecycle-producer, source) run the
// harness's videotestsrc, whose default pool allocates frame-sized system
// memory every session. On glibc those blocks come from per-thread arenas and
// some stay resident after free, which the lifecycle gate would count against
// the long-lived peer (docs/m6/lifecycle/linux-memory-steps-20261124). Those
// processes give blocks of 8 MiB or more their own mapping. FrameLink's own
// frame-sized CPU buffers are on the consumer side, which keeps glibc's default.
static const char*gSourceMalloc="default";
static void sourceMalloc(const std::string&role){
#ifdef __GLIBC__
 if(role=="producer"||role=="lifecycle-producer"||role=="source"){CHECK(mallopt(M_MMAP_THRESHOLD,8<<20)==1);}
 gSourceMalloc="producer-side mmap_threshold=8388608";
#else
 (void)role;
#endif
}
static constexpr guint WARMUP_S=2;  // excluded from steady-state distributions

// ---- process resources -----------------------------------------------------
// memKb: resident set (Linux/Android); private commit (Windows, where the
// working set is trimmed by the system and is not a growth measure).
struct Resources {double cpuMs=0;guint64 memKb=0;guint handles=0,threads=0;};
static Resources resources(){Resources r;r.handles=handles();
#ifdef _WIN32
 FILETIME c,e,k,u;if(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)){auto t=[](FILETIME f){return (double(f.dwHighDateTime)*4294967296.0+f.dwLowDateTime)/1e4;};r.cpuMs=t(k)+t(u);}
 PROCESS_MEMORY_COUNTERS_EX m{};if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof m))r.memKb=m.PrivateUsage/1024;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap!=INVALID_HANDLE_VALUE){THREADENTRY32 te{};te.dwSize=sizeof te;const DWORD self=GetCurrentProcessId();
  for(BOOL ok=Thread32First(snap,&te);ok;ok=Thread32Next(snap,&te))if(te.th32OwnerProcessID==self)r.threads++;CloseHandle(snap);}
#else
 rusage u{};if(!getrusage(RUSAGE_SELF,&u))r.cpuMs=(u.ru_utime.tv_sec+u.ru_stime.tv_sec)*1e3+(u.ru_utime.tv_usec+u.ru_stime.tv_usec)/1e3;
 if(FILE*f=fopen("/proc/self/statm","r")){unsigned long size=0,res=0;if(fscanf(f,"%lu %lu",&size,&res)==2)r.memKb=res*(unsigned long)sysconf(_SC_PAGESIZE)/1024;fclose(f);}
 if(auto*d=opendir("/proc/self/task")){while(auto*e=readdir(d))if(e->d_name[0]!='.')r.threads++;closedir(d);}
#endif
 return r;}
struct Sampler {
 std::vector<std::pair<double,Resources>> series;std::atomic<bool> run{true};std::thread th;gint64 t0=g_get_monotonic_time();
 Sampler(){th=std::thread([this]{while(run){series.push_back({(g_get_monotonic_time()-t0)/1e6,resources()});for(int i=0;i<20&&run;i++)g_usleep(50000);}});}
 void stop(){run=false;th.join();series.push_back({(g_get_monotonic_time()-t0)/1e6,resources()});}
 // Steady state: samples after warm-up and before the last (teardown) one.
 // Growth: least-squares slope (per minute) over the second half of it.
 void report(std::map<std::string,std::string>&r)const{
  std::vector<std::pair<double,Resources>> s;for(size_t i=0;i+1<series.size();i++)if(series[i].first>=WARMUP_S)s.push_back(series[i]);
  if(s.size()<2){r["resources"]="too short";return;}
  auto slope=[&](auto get){double sx=0,sy=0,sxx=0,sxy=0;size_t n=0;for(size_t i=s.size()/2;i<s.size();i++){double x=s[i].first,y=double(get(s[i].second));sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;n++;}
   return n>1&&n*sxx-sx*sx>0?(n*sxy-sx*sy)/(n*sxx-sx*sx)*60:0.0;};
  auto range=[&](auto get){guint64 lo=G_MAXUINT64,hi=0;for(auto&x:s){lo=std::min<guint64>(lo,get(x.second));hi=std::max<guint64>(hi,get(x.second));}
   return std::to_string(get(s.front().second))+".."+std::to_string(get(s.back().second))+" min="+std::to_string(lo)+" max="+std::to_string(hi);};
  auto num=[](double v){char t[32];snprintf(t,sizeof t,"%.2f",v);return std::string(t);};
  const auto&a=s.front(),&b=s.back();
  r["cpu.percent"]=num((b.second.cpuMs-a.second.cpuMs)/((b.first-a.first)*10));
#ifdef _WIN32
  r["mem.kind"]="private";
#else
  r["mem.kind"]="rss";
#endif
  r["mem.kb"]=range([](const Resources&x){return x.memKb;});r["mem.slope.kb_per_min"]=num(slope([](const Resources&x){return x.memKb;}));
  r["handles"]=range([](const Resources&x){return guint64(x.handles);});r["handles.slope.per_min"]=num(slope([](const Resources&x){return x.handles;}));
  r["threads"]=range([](const Resources&x){return guint64(x.threads);});r["threads.slope.per_min"]=num(slope([](const Resources&x){return x.threads;}));
  std::string t;for(auto&x:series){char e[96];snprintf(e,sizeof e,"%.1f:%.0f:%llu:%u:%u;",x.first,x.second.cpuMs,(unsigned long long)x.second.memKb,x.second.handles,x.second.threads);t+=e;}
  r["series.t_cpums_memkb_handles_threads"]=t;}
};

// ---- distributions -----------------------------------------------------------
// Nearest-rank percentile (p in 0..1; 0 is the minimum).
static double pct(std::vector<double> v,double p){if(v.empty())return NAN;std::sort(v.begin(),v.end());const double rank=std::ceil(p*v.size())-1;return v[size_t(std::max(0.0,std::min(rank,double(v.size()-1))))];}
static std::string dist(const std::vector<double>&v){char t[200];snprintf(t,sizeof t,"n=%zu p50=%.2f p95=%.2f p99=%.2f max=%.2f min=%.2f",v.size(),pct(v,.5),pct(v,.95),pct(v,.99),pct(v,1),pct(v,0));return t;}

// ---- children ------------------------------------------------------------------
static void writeResult(const std::string&file,const std::map<std::string,std::string>&r){FILE*f=fopen(file.c_str(),"w");CHECK(f);for(auto&kv:r)fprintf(f,"%s=%s\n",kv.first.c_str(),kv.second.c_str());fclose(f);}
static Mode modeOf(const std::string&m){return m=="element"?Mode::Element:Mode::Bin;}

// Source cost alone: the producer's test sources into fakesinks (no FrameLink),
// so the transport's own producer CPU is the producer cell minus this.
static int sourceChild(const Options&o,const std::string&out){
 Sampler sampler;std::map<std::string,std::string> r;
 const std::string t="videotestsrc is-live=true pattern=red num-buffers="+std::to_string(o.seconds*gFps)+" ! video/x-raw,format="+gFormat+",width="+std::to_string(gWidth)+",height="+std::to_string(gHeight)+",framerate="+std::to_string(gFps)+"/1 ! fakesink sync=false "
  "audiotestsrc is-live=true samplesperbuffer="+std::to_string(BLOCK)+" num-buffers="+std::to_string(o.seconds*100)+" ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! fakesink sync=false";
 GstElement*p=parse(t);gst_element_set_state(p,GST_STATE_PLAYING);auto*bus=gst_element_get_bus(p);
 auto*m=gst_bus_timed_pop_filtered(bus,gRunLimit,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));r["result"]=m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS?"EOS":m?"error: "+errorText(m):"timeout";
 if(m)gst_message_unref(m);gst_object_unref(bus);sampler.stop();gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(p);
 sampler.report(r);writeResult(out,r);return 0;}

// Producer push: running time when the video source pushes each frame, minus
// its PTS (on the session timeline shared with the consumer). A live source
// that stalls catches up with back-to-back pushes; bursts show here.
struct Pushes {const Options* options=nullptr;guint videoIndex=0,audioIndex=0,stallCount=0;gint64 stallStartUs=0,stallElapsedUs=0;
 guint faultExecuted=0,faultDroppedBlocks=0;guint64 faultDroppedSamples=0;gint64 faultStartUs=0,faultElapsedUs=0,faultGapNs=-1;
 std::mutex m;std::vector<std::pair<gint64,gint64>> v[2];GstElement*pipeline=nullptr;};  // {pts, lateness}
static bool producerFault(const std::string&f){return f=="producer-burst"||f=="audio-stall"||f=="audio-drop"||f=="audio-gap";}
static GstPadProbeReturn pushed(GstPad*pad,GstPadProbeInfo*info,gpointer d){
 auto*a=static_cast<Pushes*>(d);auto*b=GST_PAD_PROBE_INFO_BUFFER(info);
 const int track=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pad),"m6-track"));
 if(track==0)++a->videoIndex;else ++a->audioIndex;
 // Producer faults (M6.4), at the source's push, before FrameLink sees the buffer:
 //  producer-burst: the video source blocks M ms on frame N; the live source then
 //   catches up with back-to-back frames;
 //  audio-stall: the same for audio block N (10 ms blocks);
 //  audio-drop: ceil(M/10) blocks from N never reach the sink (an unsignalled
 //   underrun: a timestamp gap without DISCONT);
 //  audio-gap: the same blocks, signalled as GStreamer does: a GAP event for the
 //   missing interval and DISCONT on the next block.
 if(a->options&&producerFault(a->options->fault)){const auto&o=*a->options;const bool video=o.fault=="producer-burst";
  if((track==0)==video){const guint index=track?a->audioIndex:a->videoIndex;
   if(o.fault=="producer-burst"||o.fault=="audio-stall"){if(index==o.faultFrame){a->faultStartUs=g_get_monotonic_time();g_usleep(gulong(o.faultMs)*1000);a->faultElapsedUs=g_get_monotonic_time()-a->faultStartUs;a->faultExecuted=1;}}
   else{const guint blocks=(o.faultMs+9)/10;
    if(index>=o.faultFrame&&index<o.faultFrame+blocks){
     if(index==o.faultFrame){a->faultStartUs=g_get_monotonic_time();a->faultExecuted=1;
      if(o.fault=="audio-gap"){const GstClockTime duration=GST_BUFFER_DURATION(b)*blocks;a->faultGapNs=gint64(duration);gst_pad_push_event(pad,gst_event_new_gap(GST_BUFFER_PTS(b),duration));}}
     ++a->faultDroppedBlocks;a->faultDroppedSamples+=gst_buffer_get_size(b)/4;return GST_PAD_PROBE_DROP;}
    if(o.fault=="audio-gap"&&index==o.faultFrame+blocks){b=gst_buffer_make_writable(b);GST_BUFFER_FLAG_SET(b,GST_BUFFER_FLAG_DISCONT);GST_PAD_PROBE_INFO_DATA(info)=b;}}}}
 auto stall=[&](const char*stage){if(track==0&&a->options&&a->options->stallStage==stage&&a->videoIndex==a->options->stallFrame){
  a->stallStartUs=g_get_monotonic_time();g_usleep(gulong(a->options->stallMs)*1000);a->stallElapsedUs=g_get_monotonic_time()-a->stallStartUs;++a->stallCount;}};
 // Test-only controls: before measuring source push versus after that boundary.
 // Neither is a GPU delay injection; actual execution is reported below.
 stall("source");
 if(auto*clock=gst_element_get_clock(a->pipeline)){gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(a->pipeline))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
  std::lock_guard<std::mutex> lock(a->m);a->v[track].push_back({gint64(GST_BUFFER_PTS(b)),l});}
 stall("post-push");
 return GST_PAD_PROBE_OK;}

#ifdef FL_BENCH_GPU_READBACK
// ---- GPU-staged readback (M6.8c, Linux) ------------------------------------------
// Frame identity: row 0 starts with the frame index (8 bytes, little endian);
// the rest of the first CONTENT_ROWS rows is per-byte positional content, so a
// stale, repeated, shifted or swizzled frame fails. Only those rows are written
// and checked: the producer stands for a GPU producer, whose rendering is not
// measured here, and the staging copy moves the whole frame regardless.
// Full-frame pixels are checked by test/fl_gst_native_av_session.cpp.
static constexpr unsigned CONTENT_ROWS=4;
static guint8 contentByte(guint64 frame,unsigned x,unsigned y){return guint8(guint8(frame)*29+x*7+y*13);}
static void writeContent(GstBuffer*b,guint64 frame){
 auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_dmabuf_memory(mem));const int fd=gst_dmabuf_memory_get_fd(mem);
 auto*meta=gst_buffer_get_video_meta(b);CHECK(meta);
 dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
 const gsize bytes=meta->offset[0]+gsize(meta->stride[0])*CONTENT_ROWS;
 void*data=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(data!=MAP_FAILED);
 std::vector<guint8> rows(gsize(gWidth)*4);
 for(unsigned y=0;y<CONTENT_ROWS;++y){for(unsigned x=0;x<rows.size();++x)rows[x]=contentByte(frame,x,y);
  if(!y)for(unsigned i=0;i<8;++i)rows[i]=guint8(frame>>(8*i));
  memcpy((guint8*)data+meta->offset[0]+gsize(meta->stride[0])*y,rows.data(),rows.size());}
 CHECK(munmap(data,bytes)==0);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)==0);
}
// The application side of a native producer: the sink's pool through the
// FrameLink allocation contract, one ready eventfd per frame (CPU writes are
// complete when it is set), and live timestamps: frame n is due at t0 + n/fps
// on the pipeline clock and carries that running time as its PTS.
// A frame whose slot is not free within the sink's buffer-wait-ms is dropped,
// as the CPU-copy sink drops on a full ring; the run continues and counts it.
struct NativeFeed {std::thread th;std::string error;GstBufferPool*pool=nullptr;guint acquired=0,drops=0;std::string dropFrames;};
static void feedNative(Producer&p,guint frames,NativeFeed&feed){
 auto*bin=gst_bin_get_by_name(GST_BIN(p.p),"s");auto*src=gst_bin_get_by_name(GST_BIN(p.p),"vsrc");CHECK(bin&&src);
 auto fail=[&](const std::string&what){feed.error=what;gst_app_src_end_of_stream(GST_APP_SRC(src));};
 [&]{
  // The pool exists once the inner sink has its session.
  auto*inner=gst_bin_get_by_name(GST_BIN(bin),"video");CHECK(inner);GstState state=GST_STATE_NULL;const gint64 until=g_get_monotonic_time()+15*G_USEC_PER_SEC;
  do{gst_element_get_state(inner,&state,nullptr,0);if(state==GST_STATE_PLAYING)break;g_usleep(1000);}while(g_get_monotonic_time()<until);gst_object_unref(inner);
  if(state!=GST_STATE_PLAYING)return fail("native sink not playing");
  const std::string caps="video/x-raw(memory:DMABuf),format="+gFormat+",width="+std::to_string(gWidth)+",height="+std::to_string(gHeight)+",framerate="+std::to_string(gFps)+"/1";
  auto*c=gst_caps_from_string(caps.c_str());auto*q=gst_query_new_allocation(c,TRUE);gst_caps_unref(c);
  auto policy=readbackPolicy();guint8 wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&policy.requirements[0],wire,sizeof wire)==FL_MEMORY_OK);
  auto*bytes=g_bytes_new(wire,sizeof wire);
  gst_structure_set(gst_query_writable_structure(q),"framelink-memory-requirements",G_TYPE_BYTES,bytes,"framelink-completion-contract",G_TYPE_STRING,"external-sync-fd-before-submit-v1",nullptr);g_bytes_unref(bytes);
  auto*pad=gst_element_get_static_pad(bin,"video_sink");const bool answered=gst_pad_query(pad,q);gst_object_unref(pad);
  guint size=0,min=0,max=0;if(answered&&gst_query_get_n_allocation_pools(q))gst_query_parse_nth_allocation_pool(q,0,&feed.pool,&size,&min,&max);gst_query_unref(q);
  if(!feed.pool||!gst_buffer_pool_set_active(feed.pool,TRUE))return fail("native pool not offered");
  auto*clock=gst_element_get_clock(p.p);CHECK(clock);const GstClockTime base=gst_element_get_base_time(p.p),period=gst_util_uint64_scale(GST_SECOND,1,gFps);
  const GstClockTime t0=gst_clock_get_time(clock)-base;
  for(guint n=0;n<frames;++n){const GstClockTime due=t0+gst_util_uint64_scale(n,GST_SECOND,gFps);
   auto id=gst_clock_new_single_shot_id(clock,base+due);gst_clock_id_wait(id,nullptr);gst_clock_id_unref(id);
   GstBuffer*b=nullptr;if(gst_buffer_pool_acquire_buffer(feed.pool,&b,nullptr)!=GST_FLOW_OK){++feed.drops;if(feed.drops<=32)feed.dropFrames+=std::to_string(n)+" ";continue;}
   ++feed.acquired;writeContent(b,n);
   const int ready=eventfd(1,EFD_CLOEXEC);CHECK(ready>=0);gboolean accepted=FALSE;g_signal_emit_by_name(bin,"set-native-ready-fd",b,ready,&accepted);close(ready); // the sink keeps its own duplicate
   if(!accepted){gst_buffer_unref(b);gst_object_unref(clock);return fail("ready fd refused at frame "+std::to_string(n));}
   GST_BUFFER_PTS(b)=due;GST_BUFFER_DURATION(b)=period;
   if(gst_app_src_push_buffer(GST_APP_SRC(src),b)!=GST_FLOW_OK){gst_object_unref(clock);return fail("push failed at frame "+std::to_string(n));}}
  gst_object_unref(clock);gst_app_src_end_of_stream(GST_APP_SRC(src));
 }();
 gst_object_unref(src);gst_object_unref(bin);
}
// Consumer: every frame's identity and content rows, in order.
struct ContentCheck {guint64 next=0,checked=0,mismatches=0,gaps=0,missing=0;std::string first;std::string layout;};
static GstPadProbeReturn checkContent(GstPad*pad,GstPadProbeInfo*info,gpointer d){
 auto*c=static_cast<ContentCheck*>(d);auto*b=GST_PAD_PROBE_INFO_BUFFER(info);
 GstVideoInfo vi;auto*caps=gst_pad_get_current_caps(pad);CHECK(caps&&gst_video_info_from_caps(&vi,caps));gst_caps_unref(caps);
 gsize offset=GST_VIDEO_INFO_PLANE_OFFSET(&vi,0);gint stride=GST_VIDEO_INFO_PLANE_STRIDE(&vi,0);
 if(auto*meta=gst_buffer_get_video_meta(b)){offset=meta->offset[0];stride=meta->stride[0];}
 if(c->layout.empty())c->layout="offset="+std::to_string(offset)+" stride="+std::to_string(stride)+" size="+std::to_string(gst_buffer_get_size(b));
 GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_READ));guint64 index=0;std::string bad;
 if(m.size<offset+gsize(stride)*CONTENT_ROWS)bad="short buffer";
 else{for(unsigned i=0;i<8;++i)index|=guint64(m.data[offset+i])<<(8*i);
  // A later index is a frame lost on the way (counted); an earlier one is stale.
  if(index>c->next){++c->gaps;c->missing+=index-c->next;}else if(index<c->next)bad="index "+std::to_string(index)+" expected "+std::to_string(c->next);
  for(unsigned y=0;y<CONTENT_ROWS&&bad.empty();++y)for(unsigned x=y?0:8;x<gWidth*4;++x)if(m.data[offset+gsize(stride)*y+x]!=contentByte(index,x,y)){bad="frame "+std::to_string(index)+" byte "+std::to_string(x)+" row "+std::to_string(y);break;}}
 gst_buffer_unmap(b,&m);
 ++c->checked;if(!bad.empty()){++c->mismatches;if(c->first.empty())c->first=bad;}
 c->next=index+1;return GST_PAD_PROBE_OK;}
#endif

static int producerChild(const Options&o,const std::string&session,const std::string&out){
 Sampler sampler;std::map<std::string,std::string> r;Pushes pushes;pushes.options=&o;
 {Producer p(modeOf(o.mode),session,"av",o.seconds*gFps,80,false,o.seconds*100);pushes.pipeline=p.p;
  for(int track=0;track<2;++track){pushes.v[track].resize(size_t(o.seconds+10)*(track?100:gFps));pushes.v[track].clear();
   // With --burst-queue, push time (and any stall) is at the queue's output:
   // where FrameLink receives the buffer.
   auto*src=gst_bin_get_by_name(GST_BIN(p.p),gBurstQueues?(track?"aburst":"vburst"):(track?"asrc":"vsrc"));CHECK(src);auto*pad=gst_element_get_static_pad(src,"src");g_object_set_data(G_OBJECT(pad),"m6-track",GINT_TO_POINTER(track));gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,pushed,&pushes,nullptr);gst_object_unref(pad);gst_object_unref(src);}
#ifdef FL_BENCH_GPU_READBACK
  NativeFeed feed;
  p.start();if(gGpuReadback)feed.th=std::thread([&]{feedNative(p,o.seconds*gFps,feed);});
  if(feed.th.joinable())feed.th.join();p.finish();sampler.stop();
  if(feed.pool){gst_buffer_pool_set_active(feed.pool,FALSE);gst_object_unref(feed.pool);}
  if(gGpuReadback){r["native.acquired"]=std::to_string(feed.acquired);r["native.slot_drops"]=std::to_string(feed.drops);if(!feed.dropFrames.empty())r["native.slot_drop_frames"]=feed.dropFrames;if(!feed.error.empty()){r["native.error"]=feed.error;if(p.result=="EOS")p.result="error: "+feed.error;}}
#else
  p.start();p.finish();sampler.stop();
#endif
  r["ended_monotonic_us"]=std::to_string(p.endedAt);
  r["result"]=p.result;r["pushed.frames"]=std::to_string(p.pushedFrames.load());r["pushed.samples"]=std::to_string(p.pushedSamples.load());
  {std::lock_guard<std::mutex> lock(pushes.m);for(int track=0;track<2;++track){auto&v=pushes.v[track];std::vector<double> late,gaps;guint burst=0,maxBurst=0;const double period=1e9/(track?100:gFps);const std::string name=track?"audio":"video";
   for(size_t i=0;i<v.size();i++){if(v[i].first-v.front().first<gint64(WARMUP_S*GST_SECOND))continue;late.push_back(v[i].second/1e6);
    if(i){const double gap=double((v[i].first+v[i].second)-(v[i-1].first+v[i-1].second));gaps.push_back(gap/1e6);burst=gap<period/4?burst+1:0;maxBurst=std::max(maxBurst,burst);}}
   r[name+".push_lateness.ms"]=dist(late);r[name+".push_interval.ms"]=dist(gaps);r[name+".push_burst.max"]=std::to_string(maxBurst);
   if(o.raw||p.result!="EOS"){std::ofstream f(out+(track?".audio-push.csv":".push.csv"));f<<"pts_ns,push_lateness_ns\n";for(auto&x:v)f<<x.first<<","<<x.second<<"\n";}}}
  std::lock_guard<std::mutex> lock(p.statsMutex);
  for(auto*s:p.stats){gchar*t=gst_structure_to_string(s);r[std::string("stats.")+gst_structure_get_name(s)]=t;g_free(t);}}
 r["injection.stage"]=o.stallStage.empty()?"none":o.stallStage;r["injection.frame"]=std::to_string(o.stallFrame);
 r["injection.requested_ms"]=std::to_string(o.stallMs);r["injection.executed"]=std::to_string(pushes.stallCount);
 r["injection.start_monotonic_us"]=std::to_string(pushes.stallStartUs);r["injection.elapsed_us"]=std::to_string(pushes.stallElapsedUs);
 if(producerFault(o.fault)){r["fault.executed"]=std::to_string(pushes.faultExecuted);r["fault.start_monotonic_us"]=std::to_string(pushes.faultStartUs);r["fault.elapsed_us"]=std::to_string(pushes.faultElapsedUs);
  r["fault.dropped_blocks"]=std::to_string(pushes.faultDroppedBlocks);r["fault.dropped_samples"]=std::to_string(pushes.faultDroppedSamples);r["fault.gap_ns"]=std::to_string(pushes.faultGapNs);}
 sampler.report(r);writeResult(out,r);return 0;}

struct Arrivals {std::map<GstBuffer*,gint64> pending[2];std::vector<double> residence[2];guint sampledLevel[2]={0,0};guint unmatched[2]={0,0};std::mutex m;std::vector<std::pair<gint64,gint64>> v[2];GstElement*pipeline=nullptr;};  // {pts, lateness}
static GstPadProbeReturn arrival(GstPad*pad,GstPadProbeInfo*info,gpointer d){
 auto*a=static_cast<Arrivals*>(d);auto*b=GST_PAD_PROBE_INFO_BUFFER(info);const int track=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pad),"track"));
 {auto*q=gst_pad_get_parent_element(pad);guint level=0;g_object_get(q,"current-level-buffers",&level,nullptr);gst_object_unref(q);
  std::lock_guard<std::mutex> lock(a->m);a->sampledLevel[track]=std::max(a->sampledLevel[track],level);a->pending[track][b]=g_get_monotonic_time();}
 if(auto*clock=gst_element_get_clock(a->pipeline)){gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(a->pipeline))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
  std::lock_guard<std::mutex> lock(a->m);a->v[track].push_back({gint64(GST_BUFFER_PTS(b)),l});}
 return GST_PAD_PROBE_OK;}

static GstPadProbeReturn departed(GstPad*pad,GstPadProbeInfo*info,gpointer d){
 auto*a=static_cast<Arrivals*>(d);auto*b=GST_PAD_PROBE_INFO_BUFFER(info);
 const int track=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pad),"track"));
 std::lock_guard<std::mutex> lock(a->m);auto it=a->pending[track].find(b);
 if(it==a->pending[track].end())++a->unmatched[track];
 else{a->residence[track].push_back((g_get_monotonic_time()-it->second)/1000.0);a->pending[track].erase(it);}
 return GST_PAD_PROBE_OK;
}

static int consumerChild(const Options&o,const std::string&session,const std::string&out){
 Sampler sampler;std::map<std::string,std::string> r;Arrivals arrivals;
 {Consumer c(modeOf(o.mode),session,"av",o.native,o.localConsumer?o.seconds:0);c.checkContent=false;c.record=true;arrivals.pipeline=c.p;
  // Arrival: the sink pad of the queue in front of each appsink.
  for(int track=0;track<2;track++){auto*sink=gst_bin_get_by_name(GST_BIN(c.p),track?"aout":"vout");CHECK(sink);auto*sp=gst_element_get_static_pad(sink,"sink");auto*peer=gst_pad_get_peer(sp);CHECK(peer);
   auto*queue=gst_pad_get_parent_element(peer);auto*qs=gst_element_get_static_pad(queue,"sink");g_object_set_data(G_OBJECT(qs),"track",GINT_TO_POINTER(track));
   gst_pad_add_probe(qs,GST_PAD_PROBE_TYPE_BUFFER,arrival,&arrivals,nullptr);
   g_object_set_data(G_OBJECT(peer),"track",GINT_TO_POINTER(track));gst_pad_add_probe(peer,GST_PAD_PROBE_TYPE_BUFFER,departed,&arrivals,nullptr);for(auto*x:{(GstObject*)qs,(GstObject*)queue,(GstObject*)peer,(GstObject*)sp,(GstObject*)sink})gst_object_unref(x);}
  // Pre-fault the per-buffer records so they do not show as consumer growth.
  c.bufferLife.reserve(size_t(o.seconds+10)*gFps);for(int track=0;track<2;track++){const size_t n=size_t(o.seconds+10)*(track?100:gFps);for(auto*v:{&c.lateness[track]}){v->resize(n);v->clear();}arrivals.v[track].resize(n);arrivals.v[track].clear();arrivals.residence[track].resize(n);arrivals.residence[track].clear();}
#ifdef FL_BENCH_GPU_READBACK
  // After arrival is recorded: on the appsink's pad.
  ContentCheck content;
  if(gGpuReadback){auto*sink=gst_bin_get_by_name(GST_BIN(c.p),"vout");auto*pad=gst_element_get_static_pad(sink,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,checkContent,&content,nullptr);gst_object_unref(pad);gst_object_unref(sink);}
#endif
  struct Stall {guint frame=0,ms=0,seen=0,executed=0;gint64 startUs=0,elapsedUs=0;} stall{o.faultFrame,o.faultMs};
  if(o.fault=="consumer-stall"){auto*sink=gst_bin_get_by_name(GST_BIN(c.p),"vout");auto*pad=gst_element_get_static_pad(sink,"sink");
   gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*,GstPadProbeInfo*,gpointer d)->GstPadProbeReturn{auto*st=static_cast<Stall*>(d);
    if(++st->seen==st->frame){st->startUs=g_get_monotonic_time();g_usleep(gulong(st->ms)*1000);st->elapsedUs=g_get_monotonic_time()-st->startUs;st->executed=1;}return GST_PAD_PROBE_OK;},&stall,nullptr);
   gst_object_unref(pad);gst_object_unref(sink);}
  // GAP events reaching the audio appsink: count and summed duration.
  struct Gaps {guint n=0;guint64 ns=0;} gaps;
  {auto*sink=gst_bin_get_by_name(GST_BIN(c.p),"aout");auto*pad=gst_element_get_static_pad(sink,"sink");
   gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,[](GstPad*,GstPadProbeInfo*info,gpointer d)->GstPadProbeReturn{auto*e=GST_PAD_PROBE_INFO_EVENT(info);
    if(GST_EVENT_TYPE(e)==GST_EVENT_GAP){GstClockTime pts=0,duration=0;gst_event_parse_gap(e,&pts,&duration);auto*g=static_cast<Gaps*>(d);++g->n;if(GST_CLOCK_TIME_IS_VALID(duration))g->ns+=duration;}return GST_PAD_PROBE_OK;},&gaps,nullptr);
   gst_object_unref(pad);gst_object_unref(sink);}
  c.start();c.finish();sampler.stop();
  r["ended_monotonic_us"]=std::to_string(c.endedAt);
  r["audio.gap_events"]=std::to_string(gaps.n);r["audio.gap_event_ns"]=std::to_string(gaps.ns);
  {std::lock_guard<std::mutex> lock(c.mutex);{std::string h;for(int k=0;k<8;++k)h+=(k?",":"")+std::to_string(c.videoRefcounts[k]);r["video.callback_refcount_hist"]=h;}{std::vector<double> life;guint alive=0;for(auto&x:c.bufferLife){if(x.second)life.push_back((x.second-x.first)/1000.0);else ++alive;}r["video.buffer_life_after_callback.ms"]=dist(life);r["video.buffer_alive_at_end"]=std::to_string(alive);}r["audio.pts_jumps"]=std::to_string(c.audioJumps);r["audio.pts_jump_ns"]=std::to_string(c.audioJumpNs);}
  // Recovery: buffers from the faulted one to the last arrival over budget, on
  // the faulted track (video for consumer-stall and producer-burst).
  const bool late=o.fault=="consumer-stall"||o.fault=="producer-burst"||o.fault=="audio-stall";
  if(late){std::lock_guard<std::mutex> alock(arrivals.m);auto&av=arrivals.v[o.fault=="audio-stall"?1:0];guint last=0;bool any=false;
   for(size_t i=o.faultFrame?o.faultFrame-1:0;i<av.size();i++)if(av[i].second>gint64(gArrivalBudget)){last=guint(i);any=true;}
   r["fault.recovery_frames"]=std::to_string(any?last-(o.faultFrame-1)+1:0);}
  if(o.fault=="consumer-stall"){r["fault.executed"]=std::to_string(stall.executed);r["fault.start_monotonic_us"]=std::to_string(stall.startUs);r["fault.elapsed_us"]=std::to_string(stall.elapsedUs);}
  {std::lock_guard<std::mutex> lock(c.statsMutex);for(auto*st:c.stats){gchar*t=gst_structure_to_string(st);r[std::string("stats.")+gst_structure_get_name(st)]=t;g_free(t);}}
#ifdef FL_BENCH_GPU_READBACK
  if(gGpuReadback){r["content.checked"]=std::to_string(content.checked);r["content.mismatches"]=std::to_string(content.mismatches);r["content.index_gaps"]=std::to_string(content.gaps);r["content.missing_frames"]=std::to_string(content.missing);r["content.layout"]=content.layout;r["content.rows"]=std::to_string(CONTENT_ROWS);
   if(!content.first.empty())r["content.first_mismatch"]=content.first;}
#endif
  r["result"]=c.result;r["frames"]=std::to_string(c.frames.load());r["samples"]=std::to_string(c.samples.load());r["memory"]=c.features;
  r["audio.contiguous"]=c.contiguous?"yes":"no";
  std::lock_guard<std::mutex> lock(c.mutex);std::lock_guard<std::mutex> alock(arrivals.m);
  for(int track=0;track<2;track++){const char*n=track?"audio":"video";auto&av=arrivals.v[track];auto&rv=c.lateness[track];
   r[std::string(n)+".output_queue.admission_residence.ms"]=dist(arrivals.residence[track]);
   r[std::string(n)+".output_queue.sampled_max_buffers"]=std::to_string(arrivals.sampledLevel[track]);
   r[std::string(n)+".output_queue.unmatched"]=std::to_string(arrivals.unmatched[track]);
   r[std::string(n)+".output_queue.pending"]=std::to_string(arrivals.pending[track].size());
   if(av.empty())continue;const gint64 first=av.front().first;std::vector<double> arr,ren,gaps;guint misses=0;
   for(size_t i=0;i<av.size();i++){const bool steady=av[i].first-first>=gint64(WARMUP_S*GST_SECOND);if(av[i].second>gint64(gArrivalBudget))misses++;
    if(!steady)continue;arr.push_back(av[i].second/1e6);if(i<rv.size())ren.push_back(rv[i]/1e6);}
   // Interval between consecutive renders (wall), from render lateness + PTS.
   for(size_t i=1;i<rv.size()&&i<av.size();i++)if(av[i-1].first-first>=gint64(WARMUP_S*GST_SECOND))gaps.push_back(((av[i].first+rv[i])-(av[i-1].first+rv[i-1]))/1e6);
   r[std::string(n)+".arrival.ms"]=dist(arr);r[std::string(n)+".render.ms"]=dist(ren);r[std::string(n)+".render_interval.ms"]=dist(gaps);
   std::vector<double> allArr,allRen;for(auto&x:av)allArr.push_back(x.second/1e6);for(auto x:rv)allRen.push_back(x/1e6);
   r[std::string(n)+".arrival.all.ms"]=dist(allArr);r[std::string(n)+".render.all.ms"]=dist(allRen);
   r[std::string(n)+".deadline_misses"]=std::to_string(misses)+"/"+std::to_string(av.size());
   char t[64];snprintf(t,sizeof t,"%.2f",ren.empty()?NAN:pct(ren,.99)-pct(ren,.01));r[std::string(n)+".render_jitter.p1_p99.ms"]=t;
   if(o.raw||c.result!="EOS"){std::ofstream f(out+"."+n+".csv");f<<"pts_ns,arrival_lateness_ns,render_lateness_ns\n";for(size_t i=0;i<av.size();i++)f<<av[i].first<<","<<av[i].second<<","<<(i<rv.size()?std::to_string(rv[i]):"")<<"\n";}}
  // A/V offset: per one-second PTS window, median video minus median audio render lateness.
  std::map<gint64,std::vector<double>> w[2];gint64 first=G_MAXINT64;for(auto&v:arrivals.v)if(!v.empty())first=std::min(first,v.front().first);
  for(int track=0;track<2;track++)for(size_t i=0;i<arrivals.v[track].size()&&i<c.lateness[track].size();i++){const gint64 t=arrivals.v[track][i].first-first;if(t<gint64(WARMUP_S*GST_SECOND))continue;w[track][t/GST_SECOND].push_back(c.lateness[track][i]/1e6);}
  std::vector<double> offsets,absOffsets;for(auto&kv:w[0]){auto it=w[1].find(kv.first);if(it==w[1].end())continue;const double d=pct(kv.second,.5)-pct(it->second,.5);offsets.push_back(d);absOffsets.push_back(std::fabs(d));}
  bool queueAccounting=true;for(int t=0;t<2;++t)queueAccounting=queueAccounting&&!arrivals.unmatched[t]&&arrivals.pending[t].empty()&&arrivals.residence[t].size()==arrivals.v[t].size();
  r["queue.accounting"]=queueAccounting?"complete":"incomplete";
  r["av_offset.ms"]=dist(offsets);r["av_offset.abs.ms"]=dist(absOffsets);}
 r["scope"]=o.localConsumer?"local-consumer-control-no-transport":"transport-consumer";
 sampler.report(r);writeResult(out,r);
 if(o.localConsumer)return r["result"]=="EOS"&&r["frames"]==std::to_string(guint64(o.seconds)*gFps)&&r["samples"]==std::to_string(guint64(o.seconds)*48000)&&r["audio.contiguous"]=="yes"&&r["queue.accounting"]=="complete"?0:1;
 return 0;}

// ---- lifecycle (M6.3) ------------------------------------------------------------
// The long-lived side runs one session per cycle in a single process, each in
// <base>/c<i>/session, while the driver starts a fresh process for the other
// side per cycle. After each cycle's teardown the long-lived process records
// its memory, handles and threads; growth across cycles is a leak.
static int lifecycleChild(const Options&o,bool producer,const std::string&base,const std::string&out){
 FILE*f=fopen(out.c_str(),"w");CHECK(f);bool complete=true;
 fprintf(f,"scope=%s\n",o.localConsumer?"local-consumer-lifecycle-no-transport":"transport-lifecycle");
 for(guint i=0;i<o.cycles;i++){const std::string path=base+"/c"+std::to_string(i)+"/session";std::string result;guint64 frames=0,samples=0;
  if(producer){Producer p(modeOf(o.mode),path,"av",o.seconds*gFps,80,false,o.seconds*100);p.start();p.finish();result=p.result;frames=p.pushedFrames;samples=p.pushedSamples;}
  else{Consumer c(modeOf(o.mode),path,"av",o.native,o.localConsumer?o.seconds:0);c.checkContent=false;c.start();c.finish();result=c.result;frames=c.frames;samples=c.samples;if(!c.contiguous)result="error: audio discontinuity";}
  complete=complete&&result=="EOS"&&frames==guint64(o.seconds)*gFps&&samples==guint64(o.seconds)*48000;
  const Resources r=resources();
  fprintf(f,"cycle.%u=%s|%llu|%llu|%llu|%u|%u\n",i,result.c_str(),(unsigned long long)frames,(unsigned long long)samples,(unsigned long long)r.memKb,r.handles,r.threads);fflush(f);}
 fprintf(f,"complete=%s\n",complete?"yes":"no");fclose(f);return complete?0:1;}

// ---- driver ----------------------------------------------------------------------
static std::string selfPath(const char*argv0){
#ifdef _WIN32
 char b[MAX_PATH];DWORD n=GetModuleFileNameA(nullptr,b,MAX_PATH);return n?std::string(b,n):argv0;
#else
 char b[4096];ssize_t n=readlink("/proc/self/exe",b,sizeof b-1);return n>0?std::string(b,size_t(n)):argv0;
#endif
}
static void setEnv(const char*k,const std::string&v){
#ifdef _WIN32
 _putenv_s(k,v.c_str());SetEnvironmentVariableA(k,v.empty()?nullptr:v.c_str());
#else
 if(v.empty())unsetenv(k);else setenv(k,v.c_str(),1);
#endif
}
struct Child {long pid=0;
#ifdef _WIN32
 HANDLE h=nullptr;
#endif
};
static Child spawn(const std::string&exe,const std::vector<std::string>&args){Child c;
#ifdef _WIN32
 std::string cmd="\""+exe+"\"";for(auto&a:args)cmd+=" \""+a+"\"";STARTUPINFOA si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
 CHECK(CreateProcessA(nullptr,&cmd[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));CloseHandle(pi.hThread);c.h=pi.hProcess;c.pid=long(pi.dwProcessId);
#else
 std::vector<char*> av{const_cast<char*>(exe.c_str())};for(auto&a:args)av.push_back(const_cast<char*>(a.c_str()));av.push_back(nullptr);
 fflush(nullptr);pid_t pid=fork();CHECK(pid>=0);if(pid==0){execv(exe.c_str(),av.data());_exit(127);}c.pid=pid;
#endif
 return c;}
// Exit code, or -1 after killing a child that outlived the deadline.
// Kill a child now; await then returns its status (Linux 128+SIGKILL, Windows 99).
static void killChild(Child&c){
#ifdef _WIN32
 TerminateProcess(c.h,99);
#else
 kill(pid_t(c.pid),SIGKILL);
#endif
}
static int await(Child&c,gint64 deadline){
#ifdef _WIN32
 const gint64 left=std::max<gint64>(0,deadline-g_get_monotonic_time())/1000;
 if(WaitForSingleObject(c.h,DWORD(left))!=WAIT_OBJECT_0){TerminateProcess(c.h,1);WaitForSingleObject(c.h,INFINITE);CloseHandle(c.h);return -1;}
 DWORD code=1;GetExitCodeProcess(c.h,&code);CloseHandle(c.h);return int(code);
#else
 int status=0;while(true){pid_t w=waitpid(pid_t(c.pid),&status,WNOHANG);if(w==pid_t(c.pid))break;
  if(g_get_monotonic_time()>deadline){kill(pid_t(c.pid),SIGKILL);waitpid(pid_t(c.pid),&status,0);return -1;}g_usleep(20000);}
 return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
#endif
}
static std::map<std::string,std::string> readResult(const std::string&path){std::map<std::string,std::string> r;std::ifstream in(path);std::string line;
 while(std::getline(in,line)){auto eq=line.find('=');if(eq!=std::string::npos)r[line.substr(0,eq)]=line.substr(eq+1);}return r;}

static std::vector<std::string> optionArgs(const Options&o){
 std::vector<std::string> a{"--seconds",std::to_string(o.seconds),"--size",std::to_string(gWidth)+"x"+std::to_string(gHeight),"--fps",std::to_string(gFps),"--format",gFormat,"--mode",o.mode,"--pool",std::to_string(gVideoPool),"--video-queue",std::to_string(gVideoQueue)};
 a.insert(a.end(),{"--output-queue-buffers",std::to_string(gOutputQueueBuffers),"--arrival-budget-ms",std::to_string(gArrivalBudget/GST_MSECOND),"--latency-ms",std::to_string(gPlaybackLatency/GST_MSECOND)});if(!gOutputSync)a.push_back("--transport-only");if(!gOutputAsync)a.push_back("--no-output-preroll");
 if(!o.stallStage.empty())a.insert(a.end(),{"--video-stall-stage",o.stallStage,"--video-stall-frame",std::to_string(o.stallFrame),"--video-stall-ms",std::to_string(o.stallMs)});
 if(gRetainedDevice)a.push_back("--device-owner");if(gSharedCpu)a.push_back("--shared-cpu");if(gGpuReadback)a.push_back("--gpu-readback");if(o.native)a.push_back("--native");if(o.raw)a.push_back("--raw");if(gErrorOnRingFull)a.push_back("--error-on-ring-full");if(gBurstQueues)a.push_back("--burst-queue");if(gNoLastSample)a.push_back("--no-last-sample");
 if(o.fault=="consumer-stall"||producerFault(o.fault))a.insert(a.end(),{"--fault",o.fault,"--fault-expect",o.faultExpect,"--fault-frame",std::to_string(o.faultFrame),"--fault-ms",std::to_string(o.faultMs)});return a;}

// Least-squares slope of y over x.
static double slope(const std::vector<std::pair<double,double>>&v){double sx=0,sy=0,sxx=0,sxy=0;const double n=double(v.size());
 for(auto&p:v){sx+=p.first;sy+=p.second;sxx+=p.first*p.first;sxy+=p.first*p.second;}return n>1&&n*sxx-sx*sx>0?(n*sxy-sx*sy)/(n*sxx-sx*sx):0;}
static constexpr guint LIFECYCLE_WARMUP=5;
static int lifecycle(const Options&o,const std::string&exe,const char*plugin,const std::string&base){
 const bool restartProducer=o.restart=="producer";CHECK(restartProducer||o.restart=="consumer");
 gchar*root=g_dir_make_tmp("framelink-lifecycle-XXXXXX",nullptr);CHECK(root);const std::string dir=root;g_free(root);
 for(guint i=0;i<o.cycles;i++)CHECK(g_mkdir((dir+"/c"+std::to_string(i)).c_str(),0700)==0);
 auto opts=optionArgs(o);opts.push_back("--cycles");opts.push_back(std::to_string(o.cycles));
 auto args=[&](std::vector<std::string> a){a.insert(a.begin(),plugin);a.insert(a.end(),opts.begin(),opts.end());return a;};
 const char*longRole=restartProducer?"lifecycle-consumer":"lifecycle-producer",*freshRole=restartProducer?"producer":"consumer";
 auto longChild=spawn(exe,args({"--role",longRole,"--session-base",dir,"--result",base+"-long.txt"}));
 std::vector<std::map<std::string,std::string>> fresh;std::vector<int> freshExit;const gint64 t0=g_get_monotonic_time();
 for(guint i=0;i<o.cycles;i++){auto c=spawn(exe,args({"--role",freshRole,"--session",dir+"/c"+std::to_string(i)+"/session","--result",base+"-fresh-"+std::to_string(i)+".txt"}));
  freshExit.push_back(await(c,g_get_monotonic_time()+gint64(o.seconds+60)*G_USEC_PER_SEC));fresh.push_back(readResult(base+"-fresh-"+std::to_string(i)+".txt"));
  // Retain per-cycle evidence, including failures and independent PCM counts.
 }
 const int longExit=await(longChild,g_get_monotonic_time()+gint64(o.seconds+120)*G_USEC_PER_SEC);
 auto lr=readResult(base+"-long.txt");guint ok=0;std::string firstBad;std::vector<std::pair<double,double>> mem,hnd,thr;guint64 m0=0,m1=0;guint h0=0,h1=0,t0c=0,t1c=0;
 for(guint i=0;i<o.cycles;i++){auto it=lr.find("cycle."+std::to_string(i));std::vector<std::string> f;
  if(it!=lr.end()){std::string cell;std::istringstream in(it->second);while(std::getline(in,cell,'|'))f.push_back(cell);}
  bool good=f.size()==6&&f[0]=="EOS"&&freshExit[i]==0&&fresh[i]["result"]=="EOS";
  if(good){const std::string pf=restartProducer?fresh[i]["pushed.frames"]:f[1],ps=restartProducer?fresh[i]["pushed.samples"]:f[2],cf=restartProducer?f[1]:fresh[i]["frames"],cs=restartProducer?f[2]:fresh[i]["samples"];
   good=pf==cf&&ps==cs&&pf==std::to_string(guint64(o.seconds)*gFps)&&ps==std::to_string(guint64(o.seconds)*48000);}
  if(good)ok++;else if(firstBad.empty())firstBad="cycle "+std::to_string(i)+": long="+(f.empty()?std::string("missing"):f[0])+" fresh="+fresh[i]["result"]+" exit="+std::to_string(freshExit[i]);
  if(f.size()==6&&i>=LIFECYCLE_WARMUP){const double x=i;const guint64 mk=std::stoull(f[3]);const guint h=guint(std::stoul(f[4])),t=guint(std::stoul(f[5]));
   mem.push_back({x,double(mk)});hnd.push_back({x,double(h)});thr.push_back({x,double(t)});
   if(i==LIFECYCLE_WARMUP){m0=mk;h0=h;t0c=t;}m1=mk;h1=h;t1c=t;}}
 char t[64];std::map<std::string,std::string> s{{"label",o.label},{"seconds",std::to_string(o.seconds)},
  {"config",gFormat+" "+std::to_string(gWidth)+"x"+std::to_string(gHeight)+"@"+std::to_string(gFps)+" "+o.mode+(gSharedCpu?" shared-cpu":(o.native?" native":" cpu"))+(gRetainedDevice?" retained-device":" fresh-device")+" lifecycle restart-"+o.restart+" "+std::to_string(o.cycles)+"x"+std::to_string(o.seconds)+"s"},
  {"cycles",std::to_string(o.cycles)},{"cycles.ok",std::to_string(ok)+"/"+std::to_string(o.cycles)},{"long.exit",std::to_string(longExit)},{"long.role",longRole}};
 s["expected.frames.per_cycle"]=std::to_string(guint64(o.seconds)*gFps);
 s["expected.samples.per_cycle"]=std::to_string(guint64(o.seconds)*48000);
 s["video.pool"]=std::to_string(gVideoPool);s["video.queue"]=std::to_string(gVideoQueue);
 s["output.queue_buffers"]=std::to_string(gOutputQueueBuffers);
 s["playback.latency_ms"]=std::to_string(gPlaybackLatency/GST_MSECOND);
 s["output.sync"]=gOutputSync?"true":"false";s["output.async"]=gOutputAsync?"true":"false";
 s["harness.malloc"]=gSourceMalloc;
 if(!firstBad.empty())s["first_failure"]=firstBad;
 snprintf(t,sizeof t,"%.1f",(g_get_monotonic_time()-t0)/1e6);s["wall.s"]=t;
 s["long.mem.kb"]=std::to_string(m0)+".."+std::to_string(m1);snprintf(t,sizeof t,"%.2f",slope(mem));s["long.mem.slope.kb_per_cycle"]=t;
 s["long.handles"]=std::to_string(h0)+".."+std::to_string(h1);snprintf(t,sizeof t,"%.3f",slope(hnd));s["long.handles.slope.per_cycle"]=t;
 s["long.threads"]=std::to_string(t0c)+".."+std::to_string(t1c);snprintf(t,sizeof t,"%.3f",slope(thr));s["long.threads.slope.per_cycle"]=t;
 {std::string series;for(guint i=0;i<o.cycles;i++){auto it=lr.find("cycle."+std::to_string(i));if(it!=lr.end())series+=it->second+";";}s["long.series.result_frames_samples_memkb_handles_threads"]=series;}
 const bool complete=longExit==0&&ok==o.cycles;s["complete"]=complete?"yes":"no";
 writeResult(base+".summary.txt",s);for(auto&kv:s)printf("%s=%s\n",kv.first.c_str(),kv.second.c_str());
 return complete?0:1;}

int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#else
 CHECK(argc>1);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 Options o;std::string role,session,result,sessionBase;
 for(int i=2;i<argc;i++){std::string a=argv[i];auto next=[&]{CHECK(i+1<argc);return std::string(argv[++i]);};
  if(a=="--seconds")o.seconds=guint(std::stoul(next()));else if(a=="--fps")gFps=guint(std::stoul(next()));else if(a=="--format")gFormat=next();else if(a=="--mode")o.mode=next();
  else if(a=="--video-stall-stage")o.stallStage=next();else if(a=="--video-stall-frame")o.stallFrame=guint(std::stoul(next()));else if(a=="--video-stall-ms")o.stallMs=guint(std::stoul(next()));
  else if(a=="--size"){auto s=next();auto x=s.find('x');CHECK(x!=std::string::npos);gWidth=guint(std::stoul(s.substr(0,x)));gHeight=guint(std::stoul(s.substr(x+1)));}
  else if(a=="--output-queue-buffers"){auto n=std::stoul(next());CHECK(n<=1024);gOutputQueueBuffers=guint(n);}
  else if(a=="--arrival-budget-ms"){auto ms=std::stoul(next());CHECK(ms>0&&ms<=10000);gArrivalBudget=ms*GST_MSECOND;}
  else if(a=="--latency-ms"){auto ms=std::stoul(next());CHECK(ms<=10000);gPlaybackLatency=ms*GST_MSECOND;}else if(a=="--transport-only")gOutputSync=false;
  else if(a=="--no-output-preroll")gOutputAsync=false;
  else if(a=="--pool")gVideoPool=guint(std::stoul(next()));else if(a=="--video-queue")gVideoQueue=guint(std::stoul(next()));else if(a=="--source-only")o.sourceOnly=true;
  else if(a=="--cycles")o.cycles=guint(std::stoul(next()));else if(a=="--restart")o.restart=next();else if(a=="--session-base")sessionBase=next();
  else if(a=="--local-consumer")o.localConsumer=true;else if(a=="--shared-cpu")gSharedCpu=true;else if(a=="--gpu-readback")gGpuReadback=true;else if(a=="--device-owner")gRetainedDevice=true;else if(a=="--native")o.native=true;else if(a=="--trace")o.trace=true;else if(a=="--raw")o.raw=true;else if(a=="--out")o.out=next();else if(a=="--label")o.label=next();
  else if(a=="--fault")o.fault=next();else if(a=="--fault-expect")o.faultExpect=next();else if(a=="--fault-at-s")o.faultAtS=guint(std::stoul(next()));
  else if(a=="--fault-frame")o.faultFrame=guint(std::stoul(next()));else if(a=="--fault-ms")o.faultMs=guint(std::stoul(next()));else if(a=="--error-on-ring-full")gErrorOnRingFull=true;else if(a=="--burst-queue")gBurstQueues=true;else if(a=="--no-last-sample")gNoLastSample=true;else if(a=="--fault-bound-ms")o.faultBoundMs=guint(std::stoul(next()));
  else if(a=="--role")role=next();else if(a=="--session")session=next();else if(a=="--result")result=next();else{fprintf(stderr,"unknown option %s\n",a.c_str());return 2;}}
 CHECK(o.stallStage.empty()?(o.stallFrame==0&&o.stallMs==0):
   ((o.stallStage=="source"||o.stallStage=="post-push")&&o.stallFrame>0&&o.stallFrame<guint64(o.seconds)*gFps&&o.stallMs>0&&o.stallMs<=1000&&!o.cycles&&!o.sourceOnly&&!o.localConsumer));
 {const bool kill=o.fault=="producer-kill"||o.fault=="consumer-kill",stall=o.fault=="consumer-stall",prod=producerFault(o.fault);
  CHECK(o.fault.empty()?(o.faultExpect.empty()&&!o.faultAtS&&!o.faultFrame&&!o.faultMs):((kill||stall||prod)&&!o.cycles&&!o.sourceOnly&&!o.localConsumer&&o.stallStage.empty()&&!gGpuReadback));
  // Producer faults: N within the run (frames, or 10 ms audio blocks), and the
  // declared outcome that the documented policy gives each.
  CHECK(!prod||(o.faultFrame>0&&o.faultMs>0&&o.faultMs<=1000&&!o.faultAtS&&
   (o.fault=="producer-burst"?o.faultFrame<guint64(o.seconds)*gFps:guint64(o.faultFrame)+(o.faultMs+9)/10<guint64(o.seconds)*100)&&
   ((o.fault=="producer-burst"&&(o.faultExpect=="absorbed"||o.faultExpect=="counted-drop"))||(o.fault=="audio-stall"&&o.faultExpect=="absorbed")||
    (o.fault=="audio-drop"&&o.faultExpect=="explicit-failure")||(o.fault=="audio-gap"&&o.faultExpect=="gap-visible"))));
  CHECK(!kill||(o.faultAtS>=WARMUP_S+1&&o.faultAtS+2<=o.seconds&&!o.faultFrame&&!o.faultMs&&o.faultExpect.empty()));
  CHECK(!stall||(o.faultFrame>0&&o.faultFrame<guint64(o.seconds)*gFps&&o.faultMs>0&&o.faultMs<=5000&&(o.faultExpect=="absorbed"||o.faultExpect=="counted-drop"||o.faultExpect=="replacement-failure"||o.faultExpect=="explicit-failure")));
  CHECK(o.faultBoundMs>0&&o.faultBoundMs<=10000);
  // --burst-queue: burst faults only, with raw per-buffer records for the
  // transport-share measure.
  CHECK(!gBurstQueues||((o.fault=="producer-burst"||o.fault=="audio-stall")&&o.raw&&o.faultExpect=="absorbed"));}
 // Both lifecycle roles construct the same shared-CPU-enabled bins as one-shot
 // runs. Permit repeated sessions without changing the production lifetime.
 CHECK(!gSharedCpu||(o.mode=="bin"&&!o.native&&!o.sourceOnly&&!o.localConsumer));
 CHECK(!gRetainedDevice||(o.mode=="bin"&&!gSharedCpu&&!o.sourceOnly&&!o.localConsumer));
 // GPU-staged readback: one-shot bin sessions, exact BGRA/RGBA only.
 CHECK(!gGpuReadback||(o.mode=="bin"&&!gSharedCpu&&!o.native&&!o.sourceOnly&&!o.localConsumer&&!o.cycles&&o.stallStage.empty()&&gFormat!="NV12"));
#ifndef FL_BENCH_GPU_READBACK
 if(gGpuReadback){printf("SKIP gpu-readback (EGL staging) is not in this build\n");return 77;}
#endif
#ifdef _WIN32
 struct OwnerRelease {~OwnerRelease(){flReleaseD3D11DeviceOwner(gDeviceOwner);gDeviceOwner=nullptr;}} ownerRelease;
 if(gRetainedDevice&&!role.empty())CHECK(flCreateD3D11DeviceOwner(nullptr,&gDeviceOwner)==FL_OK);
#else
 CHECK(!gRetainedDevice);
#endif
 CHECK(!o.localConsumer||(((role=="consumer"&&!o.cycles)||(role=="lifecycle-consumer"&&o.cycles>0))&&o.mode=="bin"&&!o.native&&!o.sourceOnly&&!result.empty()));
 CHECK(o.seconds>=WARMUP_S+3&&(o.mode=="bin"||o.mode=="element"));gRunLimit=(o.seconds+30)*GST_SECOND;
 // Ring: ceil(latency x fps) + 2 images (documented sizing), at least the default 8.
 // Queue: no byte/buffer limit (frames reach 33 MB at 4K); the 1 s time bound
 // stays above the session latency.
 {bool poolSet=false,queueSet=false;for(int i=2;i<argc;i++){if(!strcmp(argv[i],"--pool"))poolSet=true;if(!strcmp(argv[i],"--video-queue"))queueSet=true;}
  // A frame that arrives within the session latency is on time, so the receive
  // queue covers that latency: a producer stall shorter than it then shows as
  // lateness instead of replacing unread frames. The ring adds the frame being
  // read and the one being drawn.
  if(!queueSet)gVideoQueue=std::min<guint>(FL_MAX_POOL_SIZE-2,std::max<guint>(2,guint(std::ceil(gPlaybackLatency/1e9*gFps))));
  if(!poolSet)gVideoPool=std::min<guint>(FL_MAX_POOL_SIZE,std::max<guint>(8,gVideoQueue+2));}
 sourceMalloc(role);
 gQueue="queue max-size-bytes=0 max-size-buffers="+std::to_string(gOutputQueueBuffers)+" max-size-time=1000000000";
 if(role=="source")return sourceChild(o,result);
 if(role=="lifecycle-producer"||role=="lifecycle-consumer")return lifecycleChild(o,role=="lifecycle-producer",sessionBase,result);
 if(role=="producer")return producerChild(o,session,result);
 if(role=="consumer")return consumerChild(o,session,result);
 setvbuf(stdout,nullptr,_IONBF,0);
 if(o.native&&!nativeFeature()){printf("SKIP native consumer memory not offered by this build\n");return 0;}
 const std::string exe=selfPath(argv[0]),base=o.out+"/"+o.label,path=sessionPath();
 auto launch=[&](const char*r){
  if(o.trace){setEnv("FRAMELINK_GST_TRACE",base+"-"+r+"-gst");setEnv("FRAMELINK_TRACE",base+"-"+r+"-core");setEnv("FRAMELINK_GST_TRACE_EVENTS","1048576");setEnv("FRAMELINK_TRACE_EVENTS","1048576");}
  auto args=std::vector<std::string>{argv[1],"--role",r,"--session",path,"--result",base+"-"+r+".txt"};auto opts=optionArgs(o);args.insert(args.end(),opts.begin(),opts.end());
  auto c=spawn(exe,args);for(const char*k:{"FRAMELINK_GST_TRACE","FRAMELINK_TRACE","FRAMELINK_GST_TRACE_EVENTS","FRAMELINK_TRACE_EVENTS"})setEnv(k,"");return c;};
 if(o.sourceOnly){auto src=launch("source");printf("pids producer=%ld consumer=0\n",src.pid);
  const int rc=await(src,g_get_monotonic_time()+gint64(o.seconds+90)*G_USEC_PER_SEC);auto sr=readResult(base+"-source.txt");
  std::map<std::string,std::string> s{{"label",o.label},{"config",gFormat+" "+std::to_string(gWidth)+"x"+std::to_string(gHeight)+"@"+std::to_string(gFps)+" source only "+std::to_string(o.seconds)+"s"},{"seconds",std::to_string(o.seconds)},{"exit",std::to_string(rc)},{"harness.malloc",gSourceMalloc}};
  for(auto&kv:sr)s["source."+kv.first]=kv.second;const bool ok=rc==0&&sr["result"]=="EOS";s["complete"]=ok?"yes":"no";
  writeResult(base+".summary.txt",s);for(auto&kv:s)printf("%s=%s\n",kv.first.c_str(),kv.second.c_str());return ok?0:1;}
 if(o.cycles)return lifecycle(o,exe,argv[1],base);
 auto consumer=launch("consumer");g_usleep(200000);auto producer=launch("producer");
 // Early pid line for external samplers (GPU counters per process).
 printf("pids producer=%ld consumer=%ld\n",producer.pid,consumer.pid);
 const gint64 started=g_get_monotonic_time(),deadline=started+gint64(o.seconds+90)*G_USEC_PER_SEC;gint64 killedAt=0;
 if(o.fault=="producer-kill"||o.fault=="consumer-kill"){g_usleep(gulong(o.faultAtS)*G_USEC_PER_SEC);killedAt=g_get_monotonic_time();killChild(o.fault=="producer-kill"?producer:consumer);}
 const int pc=await(producer,deadline),cc=await(consumer,deadline);
 auto pr=readResult(base+"-producer.txt"),cr=readResult(base+"-consumer.txt");
 std::map<std::string,std::string> s{{"label",o.label},{"config",gFormat+" "+std::to_string(gWidth)+"x"+std::to_string(gHeight)+"@"+std::to_string(gFps)+" "+o.mode+(gSharedCpu?" shared-cpu":gGpuReadback?" gpu-readback":(o.native?" native":" cpu"))+(o.trace?" traced":"")+" "+std::to_string(o.seconds)+"s"},
  {"exit",std::to_string(pc)+"/"+std::to_string(cc)},{"arrival.budget_ms",std::to_string(gArrivalBudget/GST_MSECOND)},{"playback.latency_ms",std::to_string(gPlaybackLatency/GST_MSECOND)},{"output.queue_buffers",std::to_string(gOutputQueueBuffers)},{"output.sync",gOutputSync?"true":"false"},{"video.pool",std::to_string(gVideoPool)},{"video.queue",std::to_string(gVideoQueue)},{"seconds",std::to_string(o.seconds)},{"producer.pid",std::to_string(producer.pid)},{"consumer.pid",std::to_string(consumer.pid)}};
 for(auto&kv:pr)s["producer."+kv.first]=kv.second;for(auto&kv:cr)s["consumer."+kv.first]=kv.second;
 s["expected.frames"]=std::to_string(guint64(o.seconds)*gFps);
 s["output.async"]=gOutputAsync?"true":"false";s["harness.malloc"]=gSourceMalloc;
 s["expected.samples"]=std::to_string(guint64(o.seconds)*48000);
 if(!o.fault.empty()){s["fault"]=o.fault;s["fault.bound_ms"]=std::to_string(o.faultBoundMs);
  const bool killed=o.fault=="producer-kill"||o.fault=="consumer-kill";
#ifdef _WIN32
  const int killedStatus=99;
#else
  const int killedStatus=128+SIGKILL;
#endif
  bool pass=false;
  if(killed){const bool producerKilled=o.fault=="producer-kill";auto&sr=producerKilled?cr:pr;const int victimRc=producerKilled?pc:cc,survivorRc=producerKilled?cc:pc;
   const std::string result=sr["result"];const gint64 ended=sr.count("ended_monotonic_us")?std::stoll(sr["ended_monotonic_us"]):0;
   const double detect=ended&&killedAt?(ended-killedAt)/1000.0:-1;char t[32];snprintf(t,sizeof t,"%.1f",detect);
   s["fault.at_s"]=std::to_string(o.faultAtS);s["fault.detection_ms"]=t;s["fault.survivor_result"]=result;
   pass=victimRc==killedStatus&&survivorRc==0&&result.rfind("error",0)==0&&detect>=0&&detect<=o.faultBoundMs;}
  else if(producerFault(o.fault)){s["fault.expect"]=o.faultExpect;s["fault.frame"]=std::to_string(o.faultFrame);s["fault.ms"]=std::to_string(o.faultMs);
   const bool audio=o.fault!="producer-burst";const guint period=audio?10:std::max<guint>(1,1000/gFps);
   const guint recoveryLimit=(o.faultMs+period-1)/period+2;s["fault.recovery_limit_frames"]=std::to_string(recoveryLimit);
   const bool executed=pr["fault.executed"]=="1",ended=pc==0&&cc==0;
   const bool videoComplete=cr["frames"]==s["expected.frames"]&&cr["frames"]==pr["pushed.frames"];
   const bool bothEos=pr["result"]=="EOS"&&cr["result"]=="EOS";
   const long long droppedSamples=pr["fault.dropped_samples"].empty()?-1:std::stoll(pr["fault.dropped_samples"]);
   if(o.fault=="audio-drop"){
    // Detection: the consumer's end after the first missing block was due.
    const gint64 at=pr["fault.start_monotonic_us"].empty()?0:std::stoll(pr["fault.start_monotonic_us"]),end=cr["ended_monotonic_us"].empty()?0:std::stoll(cr["ended_monotonic_us"]);
    const double detect=at&&end?(end-at)/1000.0:-1;char t[32];snprintf(t,sizeof t,"%.1f",detect);s["fault.detection_ms"]=t;s["fault.bound_ms"]=std::to_string(o.faultBoundMs);
    pass=executed&&ended&&pr["result"].find("continuity mismatch")!=std::string::npos&&cr["result"]!="EOS"&&detect>=0&&detect<=o.faultBoundMs;}
   else if(o.fault=="audio-gap"){
    // Loss is explicit: exactly the dropped samples are missing, the consumer
    // sees one PTS jump of the dropped duration and receives the GAP event.
    const long long samples=cr["samples"].empty()?-1:std::stoll(cr["samples"]),expected=std::stoll(s["expected.samples"]),gapNs=std::stoll(pr["fault.gap_ns"]);
    const long long jump=std::stoll(cr["audio.pts_jump_ns"]),gapSeen=std::stoll(cr["audio.gap_event_ns"]);
    s["fault.samples_missing"]=std::to_string(expected-samples);
    pass=executed&&ended&&bothEos&&videoComplete&&cr["queue.accounting"]=="complete"&&droppedSamples>0&&samples==expected-droppedSamples&&
     cr["audio.pts_jumps"]=="1"&&std::llabs(jump-gapNs)<=2&&cr["audio.gap_events"]=="1"&&std::llabs(gapSeen-gapNs)<=2;}
   else{
    const bool complete=ended&&bothEos&&videoComplete&&cr["queue.accounting"]=="complete"&&cr["audio.contiguous"]=="yes"&&cr["samples"]==s["expected.samples"]&&cr["samples"]==pr["pushed.samples"];
    const unsigned long recovery=cr["fault.recovery_frames"].empty()?~0ul:std::stoul(cr["fault.recovery_frames"]);
    if(o.faultExpect=="absorbed"&&gBurstQueues){
     // Transport share: per buffer, consumer arrival lateness minus producer
     // push lateness at FrameLink's input (both on the session timeline), by
     // index. Recovery: buffers from the faulted one to the last over budget.
     // The burst must have happened: the buffer after the faulted one reached
     // FrameLink's input at least M - 2 periods late (a backlog formed), and the
     // backlog drained (push lateness back under budget) at least twice as fast
     // as real time. A push returns only after FrameLink takes the buffer in, so
     // the drain rate is FrameLink's intake rate under the burst; reported.
     auto column=[](const std::string&path,int col){std::vector<gint64> v;std::ifstream in(path);std::string line;std::getline(in,line);
      while(std::getline(in,line)){std::stringstream ss(line);std::string f;for(int c=0;c<=col&&std::getline(ss,f,',');++c)if(c==col)v.push_back(std::stoll(f));}return v;};
     const auto push=column(base+(audio?"-producer.txt.audio-push.csv":"-producer.txt.push.csv"),1),arrive=column(base+(audio?"-consumer.txt.audio.csv":"-consumer.txt.video.csv"),1);
     const bool matched=!push.empty()&&push.size()==arrive.size();guint last=0;bool any=false;std::vector<double> share;
     if(matched)for(size_t i=o.faultFrame-1;i<push.size();++i){const gint64 d=arrive[i]-push[i];share.push_back(d/1e6);if(d>gint64(gArrivalBudget)){last=guint(i);any=true;}}
     const guint transportRecovery=any?last-(o.faultFrame-1)+1:0;
     const auto pts=column(base+(audio?"-producer.txt.audio-push.csv":"-producer.txt.push.csv"),0);
     bool burst=false;double drainGap=NAN;guint drain=0;const size_t n0=o.faultFrame-1;
     if(matched&&n0+1<push.size()){const gint64 backlog=gint64(o.faultMs)*GST_MSECOND-2*gint64(period)*GST_MSECOND;
      size_t j=n0+1;while(j<push.size()&&push[j]>gint64(gArrivalBudget))++j;drain=guint(j-n0);
      if(j<push.size()&&j>n0+1){drainGap=double((pts[j]+push[j])-(pts[n0+1]+push[n0+1]))/double(j-n0-1)/1e6;}
      burst=o.faultMs<3*period||(push[n0+1]>=backlog&&j<push.size()&&drainGap<=period/2.0);}
     char g[32];snprintf(g,sizeof g,"%.2f",drainGap);
     s["fault.transport_matched"]=matched?"yes":"no";s["fault.transport_recovery_frames"]=std::to_string(transportRecovery);s["fault.transport_share_after_fault.ms"]=dist(share);
     s["fault.burst"]=burst?"yes":"no";s["fault.drain_frames"]=std::to_string(drain);s["fault.drain_mean_gap_ms"]=g;s["fault.total_recovery_frames"]=cr["fault.recovery_frames"];
     pass=executed&&complete&&matched&&burst&&transportRecovery<=recoveryLimit;}
    else if(o.faultExpect=="absorbed")pass=executed&&complete&&recovery<=recoveryLimit;
    else{// counted-drop (producer-burst): every video loss counted by the producer sink.
     auto counter=[&](const char*key)->long long{const std::string&t=pr["stats.framelink-sink-stats"];auto at=t.find(std::string(key)+"=(guint64)");return at==std::string::npos?-1:std::stoll(t.substr(at+strlen(key)+10));};
     const long long dropped=counter("dropped"),delivered=cr["frames"].empty()?-1:std::stoll(cr["frames"]),pushed=std::stoll(pr["pushed.frames"]);
     s["fault.producer_dropped"]=std::to_string(dropped);
     pass=executed&&ended&&bothEos&&dropped>0&&delivered+dropped==pushed&&cr["audio.contiguous"]=="yes"&&cr["samples"]==s["expected.samples"]&&cr["queue.accounting"]=="complete";}}}
  else{s["fault.expect"]=o.faultExpect;s["fault.frame"]=std::to_string(o.faultFrame);s["fault.ms"]=std::to_string(o.faultMs);
   // Producer sink drop counters (framelink-sink-stats at EOS; absent on error).
   auto counter=[&](const char*key)->long long{const std::string&t=pr["stats.framelink-sink-stats"];auto at=t.find(std::string(key)+"=(guint64)");return at==std::string::npos?-1:std::stoll(t.substr(at+strlen(key)+10));};
   const long long dropped=counter("dropped"),ringFull=counter("ring-full-drops"),delivered=cr["frames"].empty()?-1:std::stoll(cr["frames"]),pushed=pr["pushed.frames"].empty()?-1:std::stoll(pr["pushed.frames"]);
   s["fault.producer_dropped"]=std::to_string(dropped);s["fault.producer_ring_full_drops"]=std::to_string(ringFull);s["fault.error_on_ring_full"]=gErrorOnRingFull?"true":"false";
   const bool accounted=dropped>=0&&delivered>=0&&delivered+dropped==pushed;
   const bool silentLoss=cr["result"]=="EOS"&&delivered!=pushed&&!accounted;s["fault.silent_loss"]=silentLoss?"yes":"no";s["fault.loss_accounted"]=accounted?"yes":"no";
   const bool complete=pc==0&&cc==0&&cr["queue.accounting"]=="complete"&&cr["audio.contiguous"]=="yes"&&pr["result"]=="EOS"&&cr["result"]=="EOS"&&cr["frames"]==s["expected.frames"]&&cr["frames"]==pr["pushed.frames"]&&cr["samples"]==s["expected.samples"];
   const guint period=std::max<guint>(1,1000/gFps);const guint recoveryLimit=(o.faultMs+period-1)/period+2;s["fault.recovery_limit_frames"]=std::to_string(recoveryLimit);
   if(o.faultExpect=="absorbed")pass=cr["fault.executed"]=="1"&&complete&&std::stoul(cr["fault.recovery_frames"])<=recoveryLimit;
   else if(o.faultExpect=="counted-drop")pass=cr["fault.executed"]=="1"&&pc==0&&cc==0&&pr["result"]=="EOS"&&cr["result"]=="EOS"&&accounted&&dropped>0&&ringFull==dropped&&cr["audio.contiguous"]=="yes"&&cr["queue.accounting"]=="complete"&&cr["samples"]==s["expected.samples"];
   else if(o.faultExpect=="replacement-failure")pass=cr["fault.executed"]=="1"&&!silentLoss&&pc==0&&cc==0&&cr["result"].find("replaced before it was read")!=std::string::npos&&pr["result"]!="EOS";
   else pass=cr["fault.executed"]=="1"&&!silentLoss&&pc==0&&cc==0&&pr["result"].find("pool exhausted")!=std::string::npos&&cr["result"]!="EOS";}
  s["fault.pass"]=pass?"yes":"no";s["complete"]=pass?"yes":"no";
  writeResult(base+".summary.txt",s);for(auto&kv:s)printf("%s=%s\n",kv.first.c_str(),kv.second.c_str());return pass?0:1;}
 const bool ok=pc==0&&cc==0&&cr["queue.accounting"]=="complete"&&cr["audio.contiguous"]=="yes"&&pr["result"]=="EOS"&&cr["result"]=="EOS"&&cr["frames"]==s["expected.frames"]&&cr["frames"]==pr["pushed.frames"]&&cr["samples"]==s["expected.samples"]&&cr["samples"]==pr["pushed.samples"]
  &&(!gGpuReadback||(cr["content.mismatches"]=="0"&&cr["content.checked"]==s["expected.frames"]&&cr["memory"]=="memory:SystemMemory"));
 s["complete"]=ok?"yes":"no";
 writeResult(base+".summary.txt",s);for(auto&kv:s)printf("%s=%s\n",kv.first.c_str(),kv.second.c_str());
 return ok?0:1;
}
