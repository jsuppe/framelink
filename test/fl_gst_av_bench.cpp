// M6 session benchmark: one live A/V session through the public elements or
// bins, producer and consumer in separate processes. The consumer records,
// per buffer, the arrival lateness (pipeline running time when the buffer
// leaves the FrameLink source, minus its PTS: capture-to-consumer transport)
// and the delivery lateness at appsinks (synchronized unless --transport-only); both processes sample
// CPU time, resident memory, handles and threads once a second. The driver
// prints one summary (key=value) used for the M6 baselines and budgets.
//
//   fl_gst_av_bench <plugin|static> [--seconds N] [--size WxH] [--fps F]
//     [--format RGBA|NV12|BGRA] [--mode bin|element] [--native] [--trace]
//     [--latency-ms N] [--arrival-budget-ms N] [--transport-only]
//     [--output-queue-buffers N] (0 retains the legacy time-only limit)
//     [--pool N] [--video-queue N] [--raw] [--out DIR] [--label NAME]
//
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
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

struct Options {guint seconds=60,cycles=0;std::string mode="bin",label="run",out=".",restart="producer";bool native=false,trace=false,raw=false,sourceOnly=false;};
static guint gOutputQueueBuffers=0;
static GstClockTime gArrivalBudget=LATENCY;
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
struct Pushes {std::mutex m;std::vector<std::pair<gint64,gint64>> v;GstElement*pipeline=nullptr;};  // {pts, lateness}
static GstPadProbeReturn pushed(GstPad*,GstPadProbeInfo*info,gpointer d){
 auto*a=static_cast<Pushes*>(d);auto*b=GST_PAD_PROBE_INFO_BUFFER(info);
 if(auto*clock=gst_element_get_clock(a->pipeline)){gint64 l=gint64(gst_clock_get_time(clock))-gint64(gst_element_get_base_time(a->pipeline))-gint64(GST_BUFFER_PTS(b));gst_object_unref(clock);
  std::lock_guard<std::mutex> lock(a->m);a->v.push_back({gint64(GST_BUFFER_PTS(b)),l});}
 return GST_PAD_PROBE_OK;}

static int producerChild(const Options&o,const std::string&session,const std::string&out){
 Sampler sampler;std::map<std::string,std::string> r;Pushes pushes;
 {Producer p(modeOf(o.mode),session,"av",o.seconds*gFps,80,false,o.seconds*100);pushes.pipeline=p.p;pushes.v.resize(size_t(o.seconds+10)*gFps);pushes.v.clear();
  {auto*src=gst_bin_get_by_name(GST_BIN(p.p),"vsrc");CHECK(src);auto*pad=gst_element_get_static_pad(src,"src");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,pushed,&pushes,nullptr);gst_object_unref(pad);gst_object_unref(src);}
  p.start();p.finish();sampler.stop();
  r["result"]=p.result;r["pushed.frames"]=std::to_string(p.pushedFrames.load());r["pushed.samples"]=std::to_string(p.pushedSamples.load());
  {std::lock_guard<std::mutex> lock(pushes.m);auto&v=pushes.v;std::vector<double> late,gaps;guint burst=0,maxBurst=0;const double period=1e9/gFps;
   for(size_t i=0;i<v.size();i++){if(v[i].first-v.front().first<gint64(WARMUP_S*GST_SECOND))continue;late.push_back(v[i].second/1e6);
    if(i){const double gap=double((v[i].first+v[i].second)-(v[i-1].first+v[i-1].second));gaps.push_back(gap/1e6);burst=gap<period/4?burst+1:0;maxBurst=std::max(maxBurst,burst);}}
   r["video.push_lateness.ms"]=dist(late);r["video.push_interval.ms"]=dist(gaps);r["video.push_burst.max"]=std::to_string(maxBurst);
   if(o.raw||p.result!="EOS"){std::ofstream f(out+".push.csv");f<<"pts_ns,push_lateness_ns\n";for(auto&x:v)f<<x.first<<","<<x.second<<"\n";}}
  std::lock_guard<std::mutex> lock(p.statsMutex);
  for(auto*s:p.stats){gchar*t=gst_structure_to_string(s);r[std::string("stats.")+gst_structure_get_name(s)]=t;g_free(t);}}
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
 {Consumer c(modeOf(o.mode),session,"av",o.native);c.checkContent=false;c.record=true;arrivals.pipeline=c.p;
  // Arrival: the sink pad of the queue in front of each appsink.
  for(int track=0;track<2;track++){auto*sink=gst_bin_get_by_name(GST_BIN(c.p),track?"aout":"vout");CHECK(sink);auto*sp=gst_element_get_static_pad(sink,"sink");auto*peer=gst_pad_get_peer(sp);CHECK(peer);
   auto*queue=gst_pad_get_parent_element(peer);auto*qs=gst_element_get_static_pad(queue,"sink");g_object_set_data(G_OBJECT(qs),"track",GINT_TO_POINTER(track));
   gst_pad_add_probe(qs,GST_PAD_PROBE_TYPE_BUFFER,arrival,&arrivals,nullptr);
   g_object_set_data(G_OBJECT(peer),"track",GINT_TO_POINTER(track));gst_pad_add_probe(peer,GST_PAD_PROBE_TYPE_BUFFER,departed,&arrivals,nullptr);for(auto*x:{(GstObject*)qs,(GstObject*)queue,(GstObject*)peer,(GstObject*)sp,(GstObject*)sink})gst_object_unref(x);}
  // Pre-fault the per-buffer records so they do not show as consumer growth.
  for(int track=0;track<2;track++){const size_t n=size_t(o.seconds+10)*(track?100:gFps);for(auto*v:{&c.lateness[track]}){v->resize(n);v->clear();}arrivals.v[track].resize(n);arrivals.v[track].clear();arrivals.residence[track].resize(n);arrivals.residence[track].clear();}
  c.start();c.finish();sampler.stop();
  r["result"]=c.result;r["frames"]=std::to_string(c.frames.load());r["samples"]=std::to_string(c.samples.load());r["memory"]=c.features;
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
 sampler.report(r);writeResult(out,r);return 0;}

// ---- lifecycle (M6.3) ------------------------------------------------------------
// The long-lived side runs one session per cycle in a single process, each in
// <base>/c<i>/session, while the driver starts a fresh process for the other
// side per cycle. After each cycle's teardown the long-lived process records
// its memory, handles and threads; growth across cycles is a leak.
static int lifecycleChild(const Options&o,bool producer,const std::string&base,const std::string&out){
 FILE*f=fopen(out.c_str(),"w");CHECK(f);
 for(guint i=0;i<o.cycles;i++){const std::string path=base+"/c"+std::to_string(i)+"/session";std::string result;guint64 frames=0,samples=0;
  if(producer){Producer p(modeOf(o.mode),path,"av",o.seconds*gFps,80,false,o.seconds*100);p.start();p.finish();result=p.result;frames=p.pushedFrames;samples=p.pushedSamples;}
  else{Consumer c(modeOf(o.mode),path,"av",o.native);c.checkContent=false;c.start();c.finish();result=c.result;frames=c.frames;samples=c.samples;}
  const Resources r=resources();
  fprintf(f,"cycle.%u=%s|%llu|%llu|%llu|%u|%u\n",i,result.c_str(),(unsigned long long)frames,(unsigned long long)samples,(unsigned long long)r.memKb,r.handles,r.threads);fflush(f);}
 fclose(f);return 0;}

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
 a.insert(a.end(),{"--output-queue-buffers",std::to_string(gOutputQueueBuffers),"--arrival-budget-ms",std::to_string(gArrivalBudget/GST_MSECOND),"--latency-ms",std::to_string(gPlaybackLatency/GST_MSECOND)});if(!gOutputSync)a.push_back("--transport-only");
 if(o.native)a.push_back("--native");if(o.raw)a.push_back("--raw");return a;}

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
  std::remove((base+"-fresh-"+std::to_string(i)+".txt").c_str());}
 const int longExit=await(longChild,g_get_monotonic_time()+gint64(o.seconds+120)*G_USEC_PER_SEC);
 auto lr=readResult(base+"-long.txt");guint ok=0;std::string firstBad;std::vector<std::pair<double,double>> mem,hnd,thr;guint64 m0=0,m1=0;guint h0=0,h1=0,t0c=0,t1c=0;
 for(guint i=0;i<o.cycles;i++){auto it=lr.find("cycle."+std::to_string(i));std::vector<std::string> f;
  if(it!=lr.end()){std::string cell;std::istringstream in(it->second);while(std::getline(in,cell,'|'))f.push_back(cell);}
  bool good=f.size()==6&&f[0]=="EOS"&&freshExit[i]==0&&fresh[i]["result"]=="EOS";
  if(good){const std::string pf=restartProducer?fresh[i]["pushed.frames"]:f[1],ps=restartProducer?fresh[i]["pushed.samples"]:f[2],cf=restartProducer?f[1]:fresh[i]["frames"],cs=restartProducer?f[2]:fresh[i]["samples"];
   good=pf==cf&&ps==cs&&pf==std::to_string(o.seconds*gFps);}
  if(good)ok++;else if(firstBad.empty())firstBad="cycle "+std::to_string(i)+": long="+(f.empty()?std::string("missing"):f[0])+" fresh="+fresh[i]["result"];
  if(f.size()==6&&i>=LIFECYCLE_WARMUP){const double x=i;const guint64 mk=std::stoull(f[3]);const guint h=guint(std::stoul(f[4])),t=guint(std::stoul(f[5]));
   mem.push_back({x,double(mk)});hnd.push_back({x,double(h)});thr.push_back({x,double(t)});
   if(i==LIFECYCLE_WARMUP){m0=mk;h0=h;t0c=t;}m1=mk;h1=h;t1c=t;}}
 char t[64];std::map<std::string,std::string> s{{"label",o.label},{"seconds",std::to_string(o.seconds)},
  {"config",gFormat+" "+std::to_string(gWidth)+"x"+std::to_string(gHeight)+"@"+std::to_string(gFps)+" "+o.mode+(o.native?" native":" cpu")+" lifecycle restart-"+o.restart+" "+std::to_string(o.cycles)+"x"+std::to_string(o.seconds)+"s"},
  {"cycles",std::to_string(o.cycles)},{"cycles.ok",std::to_string(ok)+"/"+std::to_string(o.cycles)},{"long.exit",std::to_string(longExit)},{"long.role",longRole}};
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
  else if(a=="--size"){auto s=next();auto x=s.find('x');CHECK(x!=std::string::npos);gWidth=guint(std::stoul(s.substr(0,x)));gHeight=guint(std::stoul(s.substr(x+1)));}
  else if(a=="--output-queue-buffers"){auto n=std::stoul(next());CHECK(n<=1024);gOutputQueueBuffers=guint(n);}
  else if(a=="--arrival-budget-ms"){auto ms=std::stoul(next());CHECK(ms>0&&ms<=10000);gArrivalBudget=ms*GST_MSECOND;}
  else if(a=="--latency-ms"){auto ms=std::stoul(next());CHECK(ms<=10000);gPlaybackLatency=ms*GST_MSECOND;}else if(a=="--transport-only")gOutputSync=false;
  else if(a=="--pool")gVideoPool=guint(std::stoul(next()));else if(a=="--video-queue")gVideoQueue=guint(std::stoul(next()));else if(a=="--source-only")o.sourceOnly=true;
  else if(a=="--cycles")o.cycles=guint(std::stoul(next()));else if(a=="--restart")o.restart=next();else if(a=="--session-base")sessionBase=next();
  else if(a=="--native")o.native=true;else if(a=="--trace")o.trace=true;else if(a=="--raw")o.raw=true;else if(a=="--out")o.out=next();else if(a=="--label")o.label=next();
  else if(a=="--role")role=next();else if(a=="--session")session=next();else if(a=="--result")result=next();else{fprintf(stderr,"unknown option %s\n",a.c_str());return 2;}}
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
  std::map<std::string,std::string> s{{"label",o.label},{"config",gFormat+" "+std::to_string(gWidth)+"x"+std::to_string(gHeight)+"@"+std::to_string(gFps)+" source only "+std::to_string(o.seconds)+"s"},{"seconds",std::to_string(o.seconds)},{"exit",std::to_string(rc)}};
  for(auto&kv:sr)s["source."+kv.first]=kv.second;const bool ok=rc==0&&sr["result"]=="EOS";s["complete"]=ok?"yes":"no";
  writeResult(base+".summary.txt",s);for(auto&kv:s)printf("%s=%s\n",kv.first.c_str(),kv.second.c_str());return ok?0:1;}
 if(o.cycles)return lifecycle(o,exe,argv[1],base);
 auto consumer=launch("consumer");g_usleep(200000);auto producer=launch("producer");
 // Early pid line for external samplers (GPU counters per process).
 printf("pids producer=%ld consumer=%ld\n",producer.pid,consumer.pid);
 const gint64 deadline=g_get_monotonic_time()+gint64(o.seconds+90)*G_USEC_PER_SEC;
 const int pc=await(producer,deadline),cc=await(consumer,deadline);
 auto pr=readResult(base+"-producer.txt"),cr=readResult(base+"-consumer.txt");
 std::map<std::string,std::string> s{{"label",o.label},{"config",gFormat+" "+std::to_string(gWidth)+"x"+std::to_string(gHeight)+"@"+std::to_string(gFps)+" "+o.mode+(o.native?" native":" cpu")+(o.trace?" traced":"")+" "+std::to_string(o.seconds)+"s"},
  {"exit",std::to_string(pc)+"/"+std::to_string(cc)},{"arrival.budget_ms",std::to_string(gArrivalBudget/GST_MSECOND)},{"playback.latency_ms",std::to_string(gPlaybackLatency/GST_MSECOND)},{"output.queue_buffers",std::to_string(gOutputQueueBuffers)},{"output.sync",gOutputSync?"true":"false"},{"video.pool",std::to_string(gVideoPool)},{"video.queue",std::to_string(gVideoQueue)},{"seconds",std::to_string(o.seconds)},{"producer.pid",std::to_string(producer.pid)},{"consumer.pid",std::to_string(consumer.pid)}};
 for(auto&kv:pr)s["producer."+kv.first]=kv.second;for(auto&kv:cr)s["consumer."+kv.first]=kv.second;
 const bool ok=pc==0&&cc==0&&cr["queue.accounting"]=="complete"&&pr["result"]=="EOS"&&cr["result"]=="EOS"&&cr["frames"]==pr["pushed.frames"]&&cr["samples"]==pr["pushed.samples"];
 s["complete"]=ok?"yes":"no";
 writeResult(base+".summary.txt",s);for(auto&kv:s)printf("%s=%s\n",kv.first.c_str(),kv.second.c_str());
 return ok?0:1;
}
