// M6 decision 13 (B2): GPU device loss through FrameLink's fences, by
// test-only injection (FRAMELINK_TEST_FAULTS, set per case by CTest). A GPU
// reset completes the fences of abandoned work WITH AN ERROR; FrameLink must
// not treat that as success.
//
//   fl_device_lost_test ready   FRAMELINK_TEST_FAULTS=ready-error=2
//     Frame 1 is delivered. Frame 2's ready fence completes with an error:
//     flAcquireFrame returns FL_DEVICE_LOST and delivers nothing, and keeps
//     returning it (terminal). Both handles close.
//   fl_device_lost_test lease   FRAMELINK_TEST_FAULTS=lease-error=1
//     The consumer leases frame 1 and completes the lease with a fence that
//     completes with an error: flCollectReadLeases returns FL_DEVICE_LOST, the
//     lease's storage is retired (the producer can reuse the slot), and the
//     consumer delivers no further frame.
//
// eventfds stand in for sync_files (as in fl_sync_fd.cpp); the injected error
// applies once the stand-in has completed, as a reset's error would.
#include "framelink.h"
#include "framelink_sync_fd.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

static int signalled() {
    int fd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);CHECK(fd>=0);
    uint64_t one=1;CHECK(write(fd,&one,sizeof(one))==sizeof(one));
    return fd;
}

static void connect(const char* mode,unsigned flags,flChannel** c,flChannel** p) {
    std::string name=std::string("device-lost-")+mode+"-"+std::to_string(getpid());
    CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,3,FL_MAP_CPU|flags,c)==FL_OK);
    for(int i=0;i<200 && !*p;++i){flOpenProducer(name.c_str(),p);if(!*p)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(*p);
}

static void submit(flChannel* p,int64_t pts) {
    flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
    int ready=signalled();CHECK(flSubmitSyncFd(p,&b,ready,pts)==FL_OK);close(ready);
}

static flResult acquire(flChannel* c,flFrame* f) {
    flResult r=FL_TIMEOUT;
    for(int i=0;i<100 && r==FL_TIMEOUT;++i)r=flAcquireFrame(c,f,10);
    return r;
}

static int ready() {
    flChannel *c=nullptr,*p=nullptr;connect("ready",0,&c,&p);
    // One at a time: the default one-frame mailbox replaces an unread frame.
    submit(p,100);
    flFrame f{};
    CHECK(acquire(c,&f)==FL_OK);CHECK(f._readyValue==1 && f.ptsNs==100);
    CHECK(flRelease(c,&f)==FL_OK);
    submit(p,200);
    flFrame lost{};lost.ptsNs=-1;
    const flResult r=acquire(c,&lost);
    fprintf(stderr,"frame 2 (ready fence completed with an error): %s\n",flResultString(r));
    CHECK(r==FL_DEVICE_LOST);
    CHECK(lost.ptsNs==-1); // nothing delivered
    CHECK(flAcquireFrame(c,&lost,10)==FL_DEVICE_LOST); // terminal
    flClose(p);flClose(c);
    puts("ready fence error: FL_DEVICE_LOST, no frame delivered, terminal");
    return 0;
}

static int lease() {
    flChannel *c=nullptr,*p=nullptr;connect("lease",FL_DEFER_RELEASE,&c,&p);
    submit(p,100);
    flFrame f{};CHECK(acquire(c,&f)==FL_OK);
    uint64_t token=0;CHECK(flLeaseFrame(c,&f,&token)==FL_OK);
    int done=signalled();CHECK(flCompleteReadLeaseSyncFd(c,token,done)==FL_OK);close(done);
    flResult r=FL_BUSY;
    for(int i=0;i<1000 && r==FL_BUSY;++i){r=flCollectReadLeases(c);if(r==FL_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    fprintf(stderr,"lease completion fence completed with an error: %s\n",flResultString(r));
    CHECK(r==FL_DEVICE_LOST);
    // The lease's storage is retired: the producer can fill every slot again.
    flBuffer b[3]{};
    for(auto& x:b)CHECK(flAcquireBuffer(p,&x,1000)==FL_OK);
    for(auto& x:b)CHECK(flCancelBuffer(p,&x)==FL_OK);
    submit(p,200);
    flFrame next{};CHECK(flAcquireFrame(c,&next,50)==FL_DEVICE_LOST); // terminal
    flClose(p);flClose(c);
    puts("lease fence error: FL_DEVICE_LOST, storage retired, terminal");
    return 0;
}

int main(int argc,char** argv) {
    if(argc==2 && !strcmp(argv[1],"ready"))return ready();
    if(argc==2 && !strcmp(argv[1],"lease"))return lease();
    fprintf(stderr,"usage: fl_device_lost_test ready|lease\n");return 2;
}
