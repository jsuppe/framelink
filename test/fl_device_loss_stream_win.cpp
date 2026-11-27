// M6 decision 13 (A3), Windows physical device loss: a consumer and a producer
// process stream frames at about 60 fps until either side gets a non-OK result,
// then report it with the time and their own device's removed reason. Driven
// by docs/m6/device-loss-d13/d13-adapter.ps1, which disables and re-enables
// the display adapter while frames flow. Not registered with CTest.
//   fl_device_loss_stream_win [SECONDS]     consumer; spawns the producer
#include "framelink_d3d11.h"
#include <d3d11.h>
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static double ms() {
    using namespace std::chrono;
    static const auto t0=steady_clock::now();
    return duration<double,std::milli>(steady_clock::now()-t0).count();
}
static void report(const char* role,flChannel* ch,const char* call,flResult r,uint64_t frames) {
    HRESULT reason=S_OK;
    if(ID3D11Device* d=flChannelD3D11Device(ch))reason=d->GetDeviceRemovedReason();
    SYSTEMTIME t;GetSystemTime(&t);
    std::printf("%s: %s=%s after %llu frames; device removed reason 0x%08lx; %02u:%02u:%02u.%03u UTC\n",role,call,flResultString(r),
                (unsigned long long)frames,(unsigned long)reason,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    std::fflush(stdout);
}
static int produce(const char* name,double seconds) {
    setvbuf(stdout,nullptr,_IONBF,0);
    flChannel* p=nullptr;flResult r=FL_NOT_FOUND;
    for(int i=0;i<200 && r!=FL_OK;++i){r=flOpenProducer(name,&p);if(r!=FL_OK)Sleep(10);}
    if(r!=FL_OK){std::printf("producer: open=%s\n",flResultString(r));return 1;}
    uint64_t frames=0;double lastNote=0;
    while(ms()<seconds*1000) {
        flBuffer b{};const double before=ms();r=flAcquireBuffer(p,&b,500);
        if(ms()-before>2000){std::printf("producer: flAcquireBuffer took %.0f ms\n",ms()-before);std::fflush(stdout);}
        if(r==FL_TIMEOUT){if(ms()-lastNote>2000){lastNote=ms();report("producer",p,"flAcquireBuffer (still)",r,frames);}continue;}
        if(r!=FL_OK){report("producer",p,"flAcquireBuffer",r,frames);break;}
        void* data=nullptr;uint64_t stride=0;
        if(flImageMap(b.image,&data,&stride)==FL_OK){memset(data,int(frames&255),size_t(stride)*32);flImageUnmap(b.image);}
        r=flSubmit(p,&b,int64_t(frames)*16666667);
        if(r!=FL_OK){report("producer",p,"flSubmit",r,frames);break;}
        ++frames;Sleep(16);
    }
    if(r==FL_OK || r==FL_TIMEOUT)std::printf("producer: ran %llu frames without a failure\n",(unsigned long long)frames);
    flClose(p);return 0;
}
int main(int argc,char** argv) {
    if(argc==5 && !strcmp(argv[1],"--producer")){FILE* f=nullptr;freopen_s(&f,argv[4],"w",stdout);return produce(argv[2],atof(argv[3]));}
    const double seconds=argc>=2?atof(argv[1]):30;
    const std::string name="loss-stream-"+std::to_string(GetCurrentProcessId());
    flChannel* c=nullptr;flResult r=flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,3,FL_MAP_CPU,&c);
    if(r!=FL_OK){std::printf("consumer: create=%s\n",flResultString(r));return 1;}
    char exe[MAX_PATH]{};GetModuleFileNameA(nullptr,exe,MAX_PATH);
    // The producer writes its own report next to the executable.
    std::string log=std::string(exe);log=log.substr(0,log.find_last_of("\\")+1)+name+"-producer.log";
    std::string command="\""+std::string(exe)+"\" --producer "+name+" "+std::to_string(seconds)+" \""+log+"\"";
    STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessA(nullptr,command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi)){std::printf("consumer: spawn failed\n");return 1;}
    std::printf("consumer: streaming for up to %.0f s\n",seconds);std::fflush(stdout);
    uint64_t frames=0;
    while(ms()<seconds*1000) {
        flFrame f{};r=flAcquireFrame(c,&f,500);
        if(r==FL_TIMEOUT)continue;
        if(r!=FL_OK){report("consumer",c,"flAcquireFrame",r,frames);break;}
        ++frames;r=flRelease(c,&f);
        if(r!=FL_OK){report("consumer",c,"flRelease",r,frames);break;}
    }
    if(r==FL_OK || r==FL_TIMEOUT)std::printf("consumer: ran %llu frames without a failure\n",(unsigned long long)frames);
    const DWORD waited=WaitForSingleObject(pi.hProcess,DWORD(seconds*1000)+20000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    if(FILE* f=nullptr;fopen_s(&f,log.c_str(),"r")==0 && f){char line[512];while(fgets(line,sizeof line,f))std::fputs(line,stdout);fclose(f);}
    if(waited!=WAIT_OBJECT_0)std::printf("producer: still running %.0f s after its run time\n",20.0);
    flClose(c);return 0;
}
