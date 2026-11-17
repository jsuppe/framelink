// M5b task12: platform qualification. Each configuration (A/V with CPU-mapped
// video, A/V with native consumer memory, video-only, audio-only) runs in all
// four producer/consumer combinations (standalone elements or bins), each in a
// fresh child process with FrameLink tracing enabled. The driver then checks,
// per run: delivery and render lag, consumer/producer latency queries, the
// audio queue and video ring bounds, EOS statistics, and exact copy accounting
// from the traces (lossless recorders, exact payload totals per operation, no
// unexpected payload operations). Accounting must agree across the four
// combinations. Factories are checked, and handle (and on POSIX thread) counts
// must stay bounded across element and bin churn, with a leak control.
//
// This is software timing on the pipeline clock; it does not measure physical
// display scanout or speaker output.
#include "fl_gst_av_harness.h"
#include <fstream>
#include <map>
#include <sstream>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

struct Config {const char*name,*tracks;bool native;};
static const Config kConfigs[]={{"av-cpu","av",false},{"av-native","av",true},{"video-only","video",false},{"audio-only","audio",false}};
static const Mode kModes[2]={Mode::Element,Mode::Bin};
static constexpr guint64 FRAME_BYTES=64*32*4;

// ---- child: one traced session -------------------------------------------
static GstElement* findChild(GstElement*e,const char*factory,const char*parentFactory){
 if(GST_IS_BIN(e)){GstElement*found=nullptr;auto*it=gst_bin_iterate_recurse(GST_BIN(e));GValue v=G_VALUE_INIT;
  while(!found&&gst_iterator_next(it,&v)==GST_ITERATOR_OK){auto*c=GST_ELEMENT(g_value_get_object(&v));auto*f=gst_element_get_factory(c);auto*parent=GST_OBJECT_PARENT(c);
   if(f&&!strcmp(GST_OBJECT_NAME(f),factory)&&(!parentFactory||(parent&&GST_IS_ELEMENT(parent)&&gst_element_get_factory(GST_ELEMENT(parent))&&!strcmp(GST_OBJECT_NAME(gst_element_get_factory(GST_ELEMENT(parent))),parentFactory))))found=GST_ELEMENT(gst_object_ref(c));
   g_value_unset(&v);}
  gst_iterator_free(it);return found;}
 return nullptr;
}
static std::string latency(GstElement*pipeline){auto*q=gst_query_new_latency();std::string r="unanswered";
 if(gst_element_query(pipeline,q)){gboolean live=FALSE;GstClockTime min=0,max=0;gst_query_parse_latency(q,&live,&min,&max);
  r=std::string(live?"live":"not-live")+" min="+std::to_string(min/GST_MSECOND)+"ms max="+(GST_CLOCK_TIME_IS_VALID(max)?std::to_string(max/GST_MSECOND)+"ms":std::string("none"));}
 gst_query_unref(q);return r;}
static guint64 field(const std::vector<GstStructure*>&stats,const char*name,const char*key){guint64 v=G_MAXUINT64;for(auto*s:stats)if(gst_structure_has_name(s,name))gst_structure_get_uint64(s,key,&v);return v;}

static int child(const Config&cfg,Mode pm,Mode cm,const char*out){
 auto path=sessionPath();Producer p(pm,path,cfg.tracks,FRAMES);Consumer c(cm,path,cfg.tracks,cfg.native);
 const bool v=std::string(cfg.tracks)!="audio",a=std::string(cfg.tracks)!="video";
 std::map<std::string,std::string> r;
 // Producer audio queue: the bounded queue inside framelinkaudiosink.
 GstElement*queue=a?findChild(p.p,"queue","framelinkaudiosink"):nullptr;CHECK(!a||queue);
 std::atomic<bool> sampling{true};guint64 highTime=0;guint highBytes=0;
 std::thread sampler([&]{while(sampling){if(queue){guint64 t=0;guint b=0;g_object_get(queue,"current-level-time",&t,"current-level-bytes",&b,nullptr);highTime=std::max(highTime,t);highBytes=std::max(highBytes,b);}g_usleep(2000);}});
 c.start();g_usleep(200000);p.start();
 // Latency queries once both pipelines are PLAYING and media flows.
 CHECK(waitFor([&]{return c.playing&&p.playing&&(v?c.frames.load()>=10:c.samples.load()>=10*BLOCK);}));
 // Source answers directly: every consumer src pad (element or bin ghost pad).
 {std::string t;for(const char*n:{"video","audio"}){if((n[0]=='v')?!v:!a)continue;GstPad*pad=nullptr;
   if(cm==Mode::Bin){auto*bin=gst_bin_get_by_name(GST_BIN(c.p),"c");pad=gst_element_get_static_pad(bin,n[0]=='v'?"video_src":"audio_src");gst_object_unref(bin);}
   else{auto*e=findChild(c.p,n[0]=='v'?"framelinksrc":"framelinkaudiosrc",nullptr);pad=gst_element_get_static_pad(e,"src");gst_object_unref(e);}
   CHECK(pad);auto*q=gst_query_new_latency();gboolean live=FALSE;GstClockTime min=0,max=0;CHECK(gst_pad_query(pad,q));gst_query_parse_latency(q,&live,&min,&max);gst_query_unref(q);gst_object_unref(pad);
   t+=std::string(t.empty()?"":" ")+n+(live?" live":" not-live")+" min="+std::to_string(min/GST_MSECOND)+"ms";}
  r["latency.sources"]=t;}
 r["latency.consumer"]=latency(c.p);r["latency.producer"]=latency(p.p);r["latency.consumer.configured"]=std::to_string(gst_pipeline_get_latency(GST_PIPELINE(c.p))/GST_MSECOND)+"ms";
 if(v){auto*src=findChild(c.p,"framelinksrc",nullptr);CHECK(src);guint pool=0;g_object_get(src,"pool-size",&pool,nullptr);r["video.pool"]=std::to_string(pool);gst_object_unref(src);}
 c.finish();p.finish();sampling=false;sampler.join();
 if(queue){guint maxBytes=0;guint64 maxTime=0;g_object_get(queue,"max-size-time",&maxTime,"max-size-bytes",&maxBytes,nullptr);
  r["audio.queue.limit"]=std::to_string(maxTime/GST_MSECOND)+"ms/"+std::to_string(maxBytes)+"B";r["audio.queue.high"]=std::to_string(highTime/GST_MSECOND)+"ms/"+std::to_string(highBytes)+"B";
  r["audio.queue.within"]=highTime<=maxTime&&highBytes<=maxBytes?"yes":"no";gst_object_unref(queue);}
 r["producer"]=p.result;r["consumer"]=c.result;r["frames"]=std::to_string(c.frames.load())+"/"+std::to_string(p.pushedFrames.load());
 r["samples"]=std::to_string(c.samples.load())+"/"+std::to_string(p.pushedSamples.load());r["memory"]=v?c.features:"none";
 r["content"]=(!v||cfg.native||c.uniform)&&c.contiguous?"exact":"wrong";r["lag"]=c.lagWithin()?"within":"outside";
 char lag[160];snprintf(lag,sizeof lag,"v %.1f..%.1f a %.1f..%.1fms",v?c.trackBest[0]/1e6:0,v?c.trackWorst[0]/1e6:0,a?c.trackBest[1]/1e6:0,a?c.trackWorst[1]/1e6:0);r["lag.ms"]=lag;
 {std::lock_guard<std::mutex> lock(p.statsMutex);
  if(v)r["stats.video"]="submitted="+std::to_string(field(p.stats,"framelink-sink-stats","submitted"))+" dropped="+std::to_string(field(p.stats,"framelink-sink-stats","dropped"))+" ring-full="+std::to_string(field(p.stats,"framelink-sink-stats","ring-full-drops"));
  if(a){gboolean shared=TRUE;for(auto*s:p.stats)if(gst_structure_has_name(s,"framelink-audio-sink-stats"))gst_structure_get_boolean(s,"shared-pcm",&shared);
   r["stats.audio"]="blocks="+std::to_string(field(p.stats,"framelink-audio-sink-stats","blocks"))+" pcm-bytes="+std::to_string(field(p.stats,"framelink-audio-sink-stats","pcm-bytes"))+" sample-frames="+std::to_string(field(p.stats,"framelink-audio-sink-stats","sample-frames"))+(shared?" shared-pcm":" serialized-pcm");}}
 FILE*f=fopen(out,"w");CHECK(f);for(auto&kv:r)fprintf(f,"%s=%s\n",kv.first.c_str(),kv.second.c_str());fclose(f);
 return 0;
}

// ---- driver ----------------------------------------------------------------
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
// Runs this executable with args; returns {exit code, pid}.
static std::pair<int,long> spawn(const std::string&exe,const std::vector<std::string>&args){
#ifdef _WIN32
 std::string cmd="\""+exe+"\"";for(auto&a:args)cmd+=" \""+a+"\"";STARTUPINFOA si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
 CHECK(CreateProcessA(nullptr,&cmd[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));
 WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return {int(code),long(pi.dwProcessId)};
#else
 std::vector<char*> av{const_cast<char*>(exe.c_str())};for(auto&a:args)av.push_back(const_cast<char*>(a.c_str()));av.push_back(nullptr);
 fflush(nullptr);pid_t pid=fork();CHECK(pid>=0);if(pid==0){execv(exe.c_str(),av.data());_exit(127);}
 int status=0;CHECK(waitpid(pid,&status,0)==pid);return {WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status),long(pid)};
#endif
}
static std::map<std::string,std::string> readResult(const std::string&path){std::map<std::string,std::string> r;std::ifstream in(path);std::string line;
 while(std::getline(in,line)){auto eq=line.find('=');if(eq!=std::string::npos)r[line.substr(0,eq)]=line.substr(eq+1);}return r;}
static std::vector<std::string> split(const std::string&s){std::vector<std::string> out;std::string cell;std::istringstream in(s);while(std::getline(in,cell,','))out.push_back(cell);return out;}

// Payload accounting from one trace file: per-operation count/bytes of rows
// with observed payload (details bit 1), successful counts of named spans, and
// the maximum retained-frame occupancy.
struct Trace {bool lossless=false;std::map<std::string,std::pair<guint64,guint64>> payload;std::map<std::string,guint64> ok;guint64 occupancy=0;bool found=false;};
static Trace readTrace(const std::string&file){Trace t;std::ifstream in(file);if(!in)return t;t.found=true;std::string line;std::vector<std::string> header;
 while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.rfind("#",0)==0){if(line.find("dropped_events=0,rejected_threads=0")!=std::string::npos)t.lossless=true;continue;}
  auto cells=split(line);if(header.empty()){header=cells;continue;}
  std::map<std::string,std::string> row;for(size_t i=0;i<header.size()&&i<cells.size();i++)row[header[i]]=cells[i];
  const guint64 details=strtoull(row["details"].c_str(),nullptr,10);const std::string op=row["operation"];
  if(details&1){auto&e=t.payload[op];e.first++;e.second+=strtoull(row["payload_bytes"].c_str(),nullptr,10);}
  if(details&4)t.occupancy=std::max<guint64>(t.occupancy,strtoull(row["occupancy"].c_str(),nullptr,10));
  if(row["result"]=="0")t.ok[op]++;}
 return t;}

static std::string describe(const Trace&t){std::string s;for(auto&kv:t.payload)s+=kv.first+" n="+std::to_string(kv.second.first)+" bytes="+std::to_string(kv.second.second)+"; ";return s.empty()?"no payload operations":s;}

// Platform payload operations at the GStreamer boundary for system-memory
// RGBA input (videotestsrc) and the consumer's memory path.
static std::vector<std::string> expectedGst(bool video,bool native){
 std::vector<std::string> ops;if(!video)return ops;
#ifdef _WIN32
 ops.push_back("gst.gpu_upload_issued");if(!native){ops.push_back("gst.gpu_readback_issued");ops.push_back("gst.cpu_copy_source");}
#else
 ops.push_back("gst.cpu_copy_sink");if(!native)ops.push_back("gst.cpu_copy_source");
#endif
 return ops;}

static guint threads(){
#if defined(_WIN32)
 return 0;
#else
 guint n=0;if(auto*d=opendir("/proc/self/task")){while(auto*e=readdir(d))if(e->d_name[0]!='.')n++;closedir(d);}return n;
#endif
}
// Element and bin churn (CPU and native consumers): handles/threads return to
// the warm-up level; a deliberate leak (16 sockets per round, well above the
// Windows handle-count drift between rounds) is caught.
static void cleanup(){
 const char*feature=nativeFeature();
 auto rounds=[&](Mode m,bool native,bool leak){std::vector<guint> hs,ts;std::vector<GSocket*> leaked;
  for(int i=0;i<8;i++){auto path=sessionPath();{Producer p(m,path,"av",10);Consumer c(m,path,"av",native);c.start();p.start();c.finish();p.finish();CHECK(p.result=="EOS"&&c.result=="EOS");}
   if(leak)for(int k=0;k<16;k++)leaked.push_back(g_socket_new(G_SOCKET_FAMILY_IPV4,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr));
#ifdef _WIN32
   // All pipelines above are destroyed. Join the global idle task cache before
   // sampling: lazy GStreamer workers retain Thread/Event/IoCompletion handles
   // across rounds, independently of FrameLink ownership. Do not raise the
   // leak tolerance; the deliberately retained sockets below must still fail.
   gst_task_cleanup_all();
#endif
   hs.push_back(handles());ts.push_back(threads());}
  for(auto*s:leaked)g_object_unref(s);
  const guint warm=*std::max_element(hs.begin(),hs.begin()+3),warmT=*std::max_element(ts.begin(),ts.begin()+3);const bool ok=hs.back()<=warm+4&&ts.back()<=warmT+2;
  std::string t;for(size_t i=0;i<hs.size();i++)t+=std::to_string(hs[i])+(ts[i]?"/"+std::to_string(ts[i]):"")+" ";
  printf("  %-7s %-6s %-12s handles%s: %s-> %s\n",name(m),native?"native":"cpu",leak?"leak-control":"churn",threads()?"/threads":"",t.c_str(),ok?"bounded":"growing");return ok;};
 printf("CASE cleanup\n");
 for(Mode m:kModes){CHECK(rounds(m,false,false));if(feature)CHECK(rounds(m,true,false));}
 CHECK(!rounds(Mode::Bin,false,true));
}
static void factories(){
 printf("CASE factories\n");
 const std::pair<const char*,std::vector<const char*>> expected[]={{"framelinksink",{"sink"}},{"framelinksrc",{"src"}},{"framelinkaudiosink",{"sink"}},
  {"framelinkaudiosrc",{"src"}},{"framelinkavsink",{"video_sink","audio_sink"}},{"framelinkavsrc",{"video_src","audio_src"}}};
 for(auto&e:expected){auto*f=gst_element_factory_find(e.first);CHECK(f);std::string pads;
  for(auto*name:e.second){bool found=false;for(auto*l=gst_element_factory_get_static_pad_templates(f);l;l=l->next)if(!strcmp(static_cast<GstStaticPadTemplate*>(l->data)->name_template,name))found=true;CHECK(found);pads+=std::string(name)+" ";}
  auto*el=gst_element_factory_create(f,nullptr);CHECK(el);gst_object_unref(el);
  printf("  %-18s %-28s pads: %s\n",e.first,gst_element_factory_get_metadata(f,GST_ELEMENT_METADATA_KLASS),pads.c_str());gst_object_unref(f);}
 const char*feature=nativeFeature();printf("  native consumer memory: %s\n",feature?feature:"not offered by this build");
}

int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#else
 CHECK(argc>1);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 if(argc==7&&!strcmp(argv[2],"--child")){for(auto&cfg:kConfigs)if(!strcmp(cfg.name,argv[3]))return child(cfg,strcmp(argv[4],"bin")?Mode::Element:Mode::Bin,strcmp(argv[5],"bin")?Mode::Element:Mode::Bin,argv[6]);return 2;}
 setvbuf(stdout,nullptr,_IONBF,0);
 factories();
 // Before any child: tracing stays disabled in this process.
 cleanup();
 const std::string exe=selfPath(argv[0]);gchar*rootDir=g_dir_make_tmp("framelink-qualification-XXXXXX",nullptr);CHECK(rootDir);const std::string dir=rootDir;g_free(rootDir);
 const char*feature=nativeFeature();bool all=true;Trace cpuTrace;
 auto traced=[&](const char*config,Mode pm,Mode cm,const std::string&tag,const char*events){
  setEnv("FRAMELINK_GST_TRACE",tag+"-gst");setEnv("FRAMELINK_TRACE",tag+"-core");setEnv("FRAMELINK_GST_TRACE_EVENTS",events);setEnv("FRAMELINK_TRACE_EVENTS",events);
  auto run=spawn(exe,{argv[1],"--child",config,name(pm),name(cm),tag+".txt"});
  for(const char*k:{"FRAMELINK_GST_TRACE","FRAMELINK_TRACE","FRAMELINK_GST_TRACE_EVENTS","FRAMELINK_TRACE_EVENTS"})setEnv(k,"");return run;};
 for(auto&cfg:kConfigs){
  if(cfg.native&&!feature){printf("CASE %s: native consumer memory not offered by this build\n",cfg.name);continue;}
  printf("CASE %s\n",cfg.name);const bool v=std::string(cfg.tracks)!="audio",a=std::string(cfg.tracks)!="video";std::string first;bool same=true;
  for(Mode pm:kModes)for(Mode cm:kModes){
   const std::string tag=dir+"/"+cfg.name+"-"+name(pm)+"-"+name(cm);
   auto run=traced(cfg.name,pm,cm,tag,"16384");
   auto r=readResult(tag+".txt");auto gt=readTrace(tag+"-gst."+std::to_string(run.second)+".csv"),ct=readTrace(tag+"-core."+std::to_string(run.second)+".csv");
   if(!strcmp(cfg.name,"av-cpu"))cpuTrace=gt;
   std::vector<std::string> fail;auto need=[&](bool ok,const std::string&what){if(!ok)fail.push_back(what);};
   need(run.first==0,"child exit "+std::to_string(run.first));
   need(r["producer"]=="EOS"&&r["consumer"]=="EOS","outcome");
   need(r["frames"]==(v?std::to_string(FRAMES)+"/"+std::to_string(FRAMES):"0/0"),"frames");
   need(r["samples"]==(a?std::to_string(guint64(BLOCKS)*BLOCK)+"/"+std::to_string(guint64(BLOCKS)*BLOCK):"0/0"),"samples");
   need(r["content"]=="exact"&&r["lag"]=="within","content/lag");
   need(r["memory"]==(v?(cfg.native?std::string(feature):std::string("memory:SystemMemory")):std::string("none")),"memory");
   // Consumer: live, configured latency-ms; producer: live (live sources).
   // Consumer pipeline: live; the configured latency-ms covers the reported
   // minimum (the application's sinks) and fits the maximum. Sources answer
   // live with no added latency. Producer pipeline: the coordinated sinks do
   // not synchronize to the clock (contract), so they answer not-live and add
   // nothing.
   {guint64 min=0,max=G_MAXUINT64;const auto&lc=r["latency.consumer"];const bool ok=sscanf(lc.c_str(),"live min=%" G_GUINT64_FORMAT "ms",&min)==1;
    const auto mx=lc.find("max=");if(mx!=std::string::npos&&lc.compare(mx,8,"max=none"))max=strtoull(lc.c_str()+mx+4,nullptr,10);
    need(ok&&r["latency.consumer.configured"]=="150ms"&&min<=150&&max>=150,"consumer latency");}
   need(r["latency.sources"]==std::string(v?"video live min=0ms":"")+(v&&a?" ":"")+(a?"audio live min=0ms":""),"source latency");
   need(r["latency.producer"]=="not-live min=0ms max=none","producer latency");
   if(v)need(r["video.pool"]=="8"&&r["stats.video"]=="submitted="+std::to_string(FRAMES)+" dropped=0 ring-full=0","video bound/stats");
   if(a)need(r["audio.queue.within"]=="yes"&&r["audio.queue.limit"].rfind("80ms/",0)==0&&
    r["stats.audio"]=="blocks="+std::to_string(BLOCKS)+" pcm-bytes="+std::to_string(guint64(BLOCKS)*BLOCK*4)+" sample-frames="+std::to_string(guint64(BLOCKS)*BLOCK)+" serialized-pcm","audio bound/stats");
   // Traces: present and lossless; exactly the expected payload operations,
   // each once per frame with exact bytes; the consumer read every frame once.
   // Audio is not traced (PCM accounting is the EOS statistics); with no
   // video activity the recorders never start and write no file.
   if(v)need(gt.found&&gt.lossless&&ct.found&&ct.lossless,"trace missing or lossy");
   else need((!gt.found||gt.lossless)&&(!ct.found||ct.lossless),"lossy trace");
   auto ops=expectedGst(v,cfg.native);need(gt.payload.size()==ops.size(),"unexpected gst payload operations");
   for(auto&op:ops){auto it=gt.payload.find(op);need(it!=gt.payload.end()&&it->second.first==FRAMES&&it->second.second==FRAMES*FRAME_BYTES,"payload "+op);}
   if(v){need(ct.ok["flEndSubmit"]==FRAMES&&ct.ok["flAcquireFrame"]==FRAMES,"core submit/acquire counts");need(gt.ok["gst.srcCreate"]>=FRAMES,"source spans");}
   else need(ct.ok["flEndSubmit"]==0&&ct.ok["flAcquireFrame"]==0&&gt.payload.empty(),"audio-only video activity");
   if(cfg.native)need(ct.occupancy>=1&&ct.occupancy<=8,"native ring occupancy");
   std::string accounting=std::string(gt.found||ct.found?"":"no trace files (no video activity); ")+"gst: "+describe(gt)+"| core: "+describe(ct)+"| occupancy max "+std::to_string(ct.occupancy);
   printf("  producer=%-7s consumer=%-7s %s frames %s samples %s memory %s lag %s\n",name(pm),name(cm),fail.empty()?"PASS":"FAIL",r["frames"].c_str(),r["samples"].c_str(),r["memory"].c_str(),r["lag.ms"].c_str());
   printf("    latency consumer %s (configured %s); sources %s; producer %s\n",r["latency.consumer"].c_str(),r["latency.consumer.configured"].c_str(),r["latency.sources"].c_str(),r["latency.producer"].c_str());
   if(v)printf("    video pool %s; producer %s\n",r["video.pool"].c_str(),r["stats.video"].c_str());
   if(a)printf("    audio queue limit %s high-water %s; %s\n",r["audio.queue.limit"].c_str(),r["audio.queue.high"].c_str(),r["stats.audio"].c_str());
   printf("    %s\n",accounting.c_str());
   for(auto&f:fail)printf("    FAILED: %s\n",f.c_str());
   all=all&&fail.empty();
   // Agreement across combinations: accounting and bounds, not timings.
   std::string summary="gst: "+describe(gt)+"| core submits "+std::to_string(ct.ok["flEndSubmit"])+" acquires "+std::to_string(ct.ok["flAcquireFrame"])+" | "+r["stats.video"]+" | "+r["stats.audio"]+" | "+r["memory"]+" | "+r["audio.queue.limit"];
   if(first.empty())first=summary;else if(summary!=first)same=false;}
  printf("  => %s\n",same?"accounting identical across element/bin combinations":"accounting DIFFERS across combinations");all=all&&same;}
 // Controls: a recorder too small for the run must read as lossy, and the
 // CPU-path accounting must not satisfy the native expectations.
 printf("CASE controls\n");
 {const std::string tag=dir+"/control-small-recorder";auto run=traced("av-cpu",Mode::Bin,Mode::Bin,tag,"16");
  auto t=readTrace(tag+"-gst."+std::to_string(run.second)+".csv");const bool caught=run.first==0&&t.found&&!t.lossless;
  printf("  16-event recorder: %s\n",caught?"lossy footer detected":"NOT DETECTED");all=all&&caught;}
 {auto native=expectedGst(true,true);const bool caught=cpuTrace.payload.size()!=native.size();
  printf("  CPU-path trace against native expectations: %s\n",caught?"rejected":"NOT REJECTED");all=all&&caught;}
 CHECK(all);
 puts("platform qualification: elements and bins pass delivery, latency, bounds, cleanup and trace accounting");
 return 0;
}
