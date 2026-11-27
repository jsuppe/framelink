// M6 decision 13 (B2), Windows: D3D11 device loss by test-only injection.
// FRAMELINK_TEST_FAULTS=device-removed-after=1 (set by CTest): once one frame
// has been acquired in this process, every fence reads UINT64_MAX and every
// device reports removed, as after a TDR or adapter removal.
//   - frame 1 is delivered normally;
//   - then the producer's flAcquireBuffer returns FL_DEVICE_LOST;
//   - the consumer's flAcquireFrame returns FL_DEVICE_LOST and delivers
//     nothing (before the fix a removed device's UINT64_MAX fence value
//     passed as readiness), and keeps returning it;
//   - both handles close.
#include "framelink.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

int main() {
    const std::string name="device-lost-win-"+std::to_string(GetCurrentProcessId());
    flChannel *c=nullptr,*p=nullptr;
    flResult r=flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_BGRA8,3,0,&c);
    if(r!=FL_OK){fprintf(stderr,"no D3D11 channel: %s\n",flResultString(r));return 77;}
    for(int i=0;i<200 && !p;++i){flOpenProducer(name.c_str(),&p);if(!p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(p);
    flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);CHECK(flSubmit(p,&b,100)==FL_OK);
    flFrame f{};r=FL_TIMEOUT;
    for(int i=0;i<200 && r==FL_TIMEOUT;++i)r=flAcquireFrame(c,&f,10);
    CHECK(r==FL_OK && f.ptsNs==100);
    CHECK(flRelease(c,&f)==FL_OK);
    // The device is now "removed".
    flBuffer b2{};r=flAcquireBuffer(p,&b2,200);
    fprintf(stderr,"producer flAcquireBuffer after removal: %s\n",flResultString(r));
    CHECK(r==FL_DEVICE_LOST);
    flFrame lost{};lost.ptsNs=-1;r=flAcquireFrame(c,&lost,50);
    fprintf(stderr,"consumer flAcquireFrame after removal: %s\n",flResultString(r));
    CHECK(r==FL_DEVICE_LOST && lost.ptsNs==-1);
    CHECK(flAcquireFrame(c,&lost,10)==FL_DEVICE_LOST); // terminal
    flClose(p);flClose(c);
    puts("device removed: FL_DEVICE_LOST on both sides, nothing delivered, terminal");
    return 0;
}
