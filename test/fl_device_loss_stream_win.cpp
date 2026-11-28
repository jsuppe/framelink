// M6 decision 13 (A3), Windows physical device loss: a consumer and a producer
// process stream frames at about 60 fps until either side gets a non-OK result,
// then report it with the time and their own device's removed reason. Driven
// by docs/m6/device-loss-d13/d13-adapter.ps1, which disables and re-enables
// the display adapter while frames flow. Not registered with CTest.
//   fl_device_loss_stream_win [SECONDS [HANG_AFTER]]   consumer; spawns the producer
// HANG_AFTER > 0 (a REAL GPU timeout reset in the producer): after that many
// frames the producer queues a compute shader that does not finish on its
// channel device, then writes and submits its next frame behind it.
#include "framelink_d3d11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
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
static void queueHang(ID3D11Device* device) {
    using Microsoft::WRL::ComPtr;
    const char* hlsl="RWBuffer<uint> o:register(u0);cbuffer C:register(b0){uint count;uint3 pad;};"
                     "[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){uint x=id.x;[loop]for(uint i=0;i<count;i++){x=x*1664525u+1013904223u;}o[id.x]=x;}";
    ComPtr<ID3DBlob> code;D3DCompile(hlsl,strlen(hlsl),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,nullptr);
    ComPtr<ID3D11ComputeShader> cs;device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=65536*4;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;ComPtr<ID3D11Buffer> ob;device->CreateBuffer(&bd,nullptr,&ob);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_UINT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=65536;
    ComPtr<ID3D11UnorderedAccessView> uav;device->CreateUnorderedAccessView(ob.Get(),&ud,&uav);
    const uint32_t constants[4]={0xFFFFFFFFu,0,0,0};D3D11_BUFFER_DESC cd{};cd.ByteWidth=16;cd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{constants,0,0};ComPtr<ID3D11Buffer> cb;device->CreateBuffer(&cd,&init,&cb);
    ComPtr<ID3D11DeviceContext> ctx;device->GetImmediateContext(&ctx);
    ctx->CSSetShader(cs.Get(),nullptr,0);ctx->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);ctx->CSSetConstantBuffers(0,1,cb.GetAddressOf());
    ctx->Dispatch(1024,1,1);ctx->Flush();
}
static int produce(const char* name,double seconds,uint64_t hangAfter=0) {
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
        if(hangAfter && frames==hangAfter){std::printf("producer: queueing the hang before frame %llu\n",(unsigned long long)frames);queueHang(flChannelD3D11Device(p));}
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
    if(argc==6 && !strcmp(argv[1],"--producer")){FILE* f=nullptr;freopen_s(&f,argv[4],"w",stdout);return produce(argv[2],atof(argv[3]),_strtoui64(argv[5],nullptr,10));}
    const double seconds=argc>=2?atof(argv[1]):30;
    const std::string hangAfter=argc>=3?argv[2]:"0";
    const std::string name="loss-stream-"+std::to_string(GetCurrentProcessId());
    flChannel* c=nullptr;flResult r=flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,3,FL_MAP_CPU,&c);
    if(r!=FL_OK){std::printf("consumer: create=%s\n",flResultString(r));return 1;}
    char exe[MAX_PATH]{};GetModuleFileNameA(nullptr,exe,MAX_PATH);
    // The producer writes its own report next to the executable.
    std::string log=std::string(exe);log=log.substr(0,log.find_last_of("\\")+1)+name+"-producer.log";
    std::string command="\""+std::string(exe)+"\" --producer "+name+" "+std::to_string(seconds)+" \""+log+"\" "+hangAfter;
    STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessA(nullptr,command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi)){std::printf("consumer: spawn failed\n");return 1;}
    std::printf("consumer: streaming for up to %.0f s\n",seconds);std::fflush(stdout);
    uint64_t frames=0;
    while(ms()<seconds*1000) {
        flFrame f{};r=flAcquireFrame(c,&f,500);
        if(r==FL_TIMEOUT)continue;
        if(r!=FL_OK){report("consumer",c,"flAcquireFrame",r,frames);break;}
        // A frame whose write queued behind the producer's hang: record what was delivered.
        if(hangAfter!="0" && f.sequence>=_strtoui64(hangAfter.c_str(),nullptr,10)){void* d=nullptr;uint64_t st=0;
            if(flImageMap(f.image,&d,&st)==FL_OK){std::printf("consumer: delivered frame sequence %llu after the hang; first byte %u (written value %llu)\n",
                (unsigned long long)f.sequence,((unsigned char*)d)[0],(unsigned long long)(f.sequence&255));flImageUnmap(f.image);}}
        ++frames;r=flRelease(c,&f);
        if(r!=FL_OK){report("consumer",c,"flRelease",r,frames);break;}
    }
    if(r==FL_OK || r==FL_TIMEOUT)std::printf("consumer: ran %llu frames without a failure\n",(unsigned long long)frames);
    const DWORD waited=WaitForSingleObject(pi.hProcess,DWORD(seconds*1000)+20000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    if(FILE* f=nullptr;fopen_s(&f,log.c_str(),"r")==0 && f){char line[512];while(fgets(line,sizeof line,f))std::fputs(line,stdout);fclose(f);}
    if(waited!=WAIT_OBJECT_0)std::printf("producer: still running %.0f s after its run time\n",20.0);
    flClose(c);return 0;
}
