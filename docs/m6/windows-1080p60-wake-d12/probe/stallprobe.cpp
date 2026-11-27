// FrameLink-free scheduling and file-write stall probe for Windows.
// Mirrors the rate harness's thread shapes without any FrameLink, GStreamer or GPU call:
//   video: 30 Hz (--video-hz 60 for the 1080p60 cells) paced with the harness's pace() (Sleep then
//          SwitchToThread), one CSV row per tick; --fill-mb N fills an N MB buffer each tick, as the
//          harness source fills its frame before the upload (decision 12, A2; no GPU call)
//   pcm:   100 Hz paced the same way, one CSV row per tick
//   tick:  Sleep(1) loop, no I/O
// Logs every wake-up later than 20 ms and every row write longer than 20 ms, with wall-clock
// time, to events.csv. On an event of at least --trigger-ms it saves the running WPR trace
// (wpr -stop) and restarts it, at most --max-saves times, at least 60 s apart.
// Usage: stallprobe <out-dir> <seconds> [--wpr "<wpr start args>"] [--trigger-ms 50] [--max-saves 3] [--log-ms 20]
//                   [--video-hz 30|60] [--fill-mb N] [--no-csv]
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstring>

static LARGE_INTEGER gFreq;
static uint64_t now(){LARGE_INTEGER c;QueryPerformanceCounter(&c);return uint64_t(c.QuadPart/gFreq.QuadPart)*1000000000ULL+uint64_t(c.QuadPart%gFreq.QuadPart)*1000000000ULL/uint64_t(gFreq.QuadPart);}
static void pace(uint64_t due){for(;;){auto current=now();if(current>=due)break;auto left=due-current;if(left>2000000)Sleep(DWORD(left/1000000-1));else SwitchToThread();}}
static std::string wall(){FILETIME ft;GetSystemTimePreciseAsFileTime(&ft);FILETIME lt;FileTimeToLocalFileTime(&ft,&lt);SYSTEMTIME st;FileTimeToSystemTime(&lt,&st);
 ULARGE_INTEGER u{};u.LowPart=lt.dwLowDateTime;u.HighPart=lt.dwHighDateTime;char b[64];snprintf(b,sizeof b,"%04u-%02u-%02uT%02u:%02u:%02u.%04u",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,unsigned((u.QuadPart/1000)%10000));return b;}

static std::string gOut,gWprArgs;static double gTriggerMs=50;static uint64_t gLogNs=20000000;static int gMaxSaves=3;static unsigned gVideoHz=30;static size_t gFillBytes=0;static bool gCsv=true;
static std::mutex gLock;static FILE*gEvents;static std::atomic<int> gSaves{0};static std::atomic<uint64_t> gLastSave{0};static std::atomic<bool> gSaving{false};
static std::atomic<bool> gDone{false};

static int run(const std::string&cmd){STARTUPINFOA si{sizeof si};PROCESS_INFORMATION pi{};std::string c=cmd;
 if(!CreateProcessA(nullptr,c.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return -1;WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return int(code);}

static void maybeSave(const char*what,double ms){
 if(gWprArgs.empty()||ms<gTriggerMs||gSaves.load()>=gMaxSaves)return;
 const uint64_t t=now();if(gLastSave.load()&&t-gLastSave.load()<60000000000ULL)return;
 bool expected=false;if(!gSaving.compare_exchange_strong(expected,true))return;
 const int n=++gSaves;gLastSave=t;
 std::thread([n,what=std::string(what),ms]{Sleep(2000); // keep the events after the stall in the buffer too
  const std::string etl=gOut+"\\stall-"+std::to_string(n)+".etl";const std::string w=wall();
  const int stop=run("wpr.exe -stop \""+etl+"\" \""+what+" "+std::to_string(ms)+" ms\"");
  const int start=gSaves.load()<gMaxSaves?run("wpr.exe "+gWprArgs):0;
  {std::lock_guard<std::mutex> g(gLock);fprintf(gEvents,"%s,save,%d,%s,stop=%d restart=%d\n",w.c_str(),n,etl.c_str(),stop,start);fflush(gEvents);}
  printf("%s saved %s (stop=%d restart=%d)\n",w.c_str(),etl.c_str(),stop,start);fflush(stdout);gSaving=false;}).detach();}

static void event(const char*thread,const char*kind,uint64_t index,double ms){
 const std::string w=wall();{std::lock_guard<std::mutex> g(gLock);fprintf(gEvents,"%s,%s,%s,%llu,%.3f\n",w.c_str(),thread,kind,(unsigned long long)index,ms);fflush(gEvents);}
 printf("%s %s %s #%llu %.3f ms\n",w.c_str(),thread,kind,(unsigned long long)index,ms);fflush(stdout);maybeSave(thread,ms);}

struct Stats{std::atomic<uint64_t> maxLateNs{0},maxWriteNs{0},n{0},over5{0},over20{0};};
static void note(std::atomic<uint64_t>&m,uint64_t v){uint64_t c=m.load();while(v>c&&!m.compare_exchange_weak(c,v)){}}

static void paced(const char*name,unsigned hz,uint64_t start,uint64_t end,Stats&s){
 std::ofstream f(gOut+"\\"+name+".csv");f<<"index,due_ns,woke_ns,written_ns\n";
 std::vector<unsigned char> fill(hz==gVideoHz&&!strcmp(name,"video")?gFillBytes:0);
 const uint64_t period=1000000000ULL/hz;for(uint64_t i=0;;++i){const uint64_t due=start+i*period;if(due>=end)break;pace(due);const uint64_t woke=now();
  if(!fill.empty())std::fill(fill.begin(),fill.end(),uint8_t(i));
  if(gCsv)f<<i<<','<<due<<','<<woke<<','; // the harness writes its row after the frame's work; the row write is timed on its own
  const uint64_t before=now();if(gCsv)f<<before<<'\n';const uint64_t written=now();
  const uint64_t late=woke-due,write=written-before;note(s.maxLateNs,late);note(s.maxWriteNs,write);++s.n;if(late>5000000)++s.over5;if(late>20000000)++s.over20;
  if(late>gLogNs)event(name,"wake",i,late/1e6);if(write>gLogNs)event(name,"write",i,write/1e6);}
}
static void tick(uint64_t end,Stats&s){for(uint64_t i=0;;++i){const uint64_t a=now();if(a>=end)break;Sleep(1);const uint64_t b=now();const uint64_t over=b-a;note(s.maxLateNs,over);++s.n;if(over>20000000)++s.over20;if(over>gLogNs){event("tick","sleep1",i,over/1e6);}}}

int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"usage: stallprobe <out-dir> <seconds> [--wpr \"<args>\"] [--trigger-ms N] [--max-saves N]\n");return 2;}
 QueryPerformanceFrequency(&gFreq);gOut=argv[1];const unsigned seconds=unsigned(atoi(argv[2]));
 for(int i=3;i<argc;++i){std::string a=argv[i];if(a=="--wpr"&&i+1<argc)gWprArgs=argv[++i];else if(a=="--trigger-ms"&&i+1<argc)gTriggerMs=atof(argv[++i]);else if(a=="--log-ms"&&i+1<argc)gLogNs=uint64_t(atof(argv[++i])*1e6);else if(a=="--max-saves"&&i+1<argc)gMaxSaves=atoi(argv[++i]);else if(a=="--video-hz"&&i+1<argc)gVideoHz=unsigned(atoi(argv[++i]));else if(a=="--fill-mb"&&i+1<argc)gFillBytes=size_t(atof(argv[++i])*1048576);else if(a=="--no-csv")gCsv=false;else{fprintf(stderr,"unknown option %s\n",a.c_str());return 2;}}
 if(gVideoHz!=30&&gVideoHz!=60)return 2;
 CreateDirectoryA(gOut.c_str(),nullptr);gEvents=fopen((gOut+"\\events.csv").c_str(),"w");if(!gEvents)return 3;fprintf(gEvents,"wall,thread,kind,index,ms\n");fflush(gEvents);
 if(!gWprArgs.empty()){run("wpr.exe -cancel");const int r=run("wpr.exe "+gWprArgs);printf("wpr start %d\n",r);fflush(stdout);if(r!=0)return 4;}
 printf("%s start %u s, trigger %.0f ms, video %u Hz, fill %zu bytes, csv %d\n",wall().c_str(),seconds,gTriggerMs,gVideoHz,gFillBytes,int(gCsv));fflush(stdout);
 const uint64_t start=now()+100000000ULL,end=start+uint64_t(seconds)*1000000000ULL;Stats v,p,t;
 std::thread tv(paced,"video",gVideoHz,start,end,std::ref(v)),tp(paced,"pcm",100u,start,end,std::ref(p)),tt(tick,end,std::ref(t));
 std::thread rep([&]{while(!gDone){for(int i=0;i<600&&!gDone;++i)Sleep(100);if(gDone)break;
  printf("%s video max %.2f/%.2f ms (wake/write) >5ms %llu | pcm max %.2f/%.2f >5ms %llu | tick max %.2f >20ms %llu\n",wall().c_str(),v.maxLateNs/1e6,v.maxWriteNs/1e6,(unsigned long long)v.over5.load(),p.maxLateNs/1e6,p.maxWriteNs/1e6,(unsigned long long)p.over5.load(),t.maxLateNs/1e6,(unsigned long long)t.over20.load());fflush(stdout);}});
 tv.join();tp.join();tt.join();gDone=true;rep.join();while(gSaving)Sleep(100);
 if(!gWprArgs.empty())run("wpr.exe -cancel");
 printf("%s done: video n=%llu wake max %.2f ms >5ms %llu >20ms %llu write max %.2f | pcm n=%llu wake max %.2f >5ms %llu >20ms %llu write max %.2f | tick n=%llu max %.2f >20ms %llu | saves %d\n",wall().c_str(),
  (unsigned long long)v.n.load(),v.maxLateNs/1e6,(unsigned long long)v.over5.load(),(unsigned long long)v.over20.load(),v.maxWriteNs/1e6,
  (unsigned long long)p.n.load(),p.maxLateNs/1e6,(unsigned long long)p.over5.load(),(unsigned long long)p.over20.load(),p.maxWriteNs/1e6,
  (unsigned long long)t.n.load(),t.maxLateNs/1e6,(unsigned long long)t.over20.load(),gSaves.load());
 fclose(gEvents);return 0;}
