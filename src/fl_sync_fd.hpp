#pragma once
#include <poll.h>
#include <linux/sync_file.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <deque>
#include <cstdint>
#include <utility>
namespace fl {
inline bool fenceReady(int fd) {
    if(fd<0)return true;
    pollfd p{fd,POLLIN,0};
    return poll(&p,1,0)>0 && (p.revents&POLLIN);
}

// A fence's state, including completion WITH AN ERROR, which a GPU reset
// leaves on the fences of work it abandoned. poll() reports such a fence as
// readable like a successful one; only SYNC_IOC_FILE_INFO's status tells them
// apart (1 signaled, 0 active, negative: signaled with that error). An fd that
// is not a sync_file (tests use eventfds) falls back to poll().
enum class FenceState { Pending, Signaled, Error };

// Test-only fault injection (M6 decision 13, B2). FRAMELINK_TEST_FAULTS is a
// comma-separated list read once per process; unset (the normal case) it does
// nothing. ready-error=V: the ready fence of the frame with ready value V
// reports an error. lease-error=N: the Nth read-lease completion fence
// (1-based, per process) reports an error.
struct TestFaults {
    uint64_t readyError=0, leaseError=0;
    static const TestFaults& get() {
        static const TestFaults faults=[]{
            TestFaults f;
            const char* env=getenv("FRAMELINK_TEST_FAULTS");
            for(const char* p=env;p && *p;) {
                const char* end=strchr(p,',');if(!end)end=p+strlen(p);
                unsigned long long v=0;
                if(sscanf(p,"ready-error=%llu",&v)==1)f.readyError=v;
                else if(sscanf(p,"lease-error=%llu",&v)==1)f.leaseError=v;
                p=*end?end+1:end;
            }
            return f;
        }();
        return faults;
    }
};

inline FenceState fenceState(int fd) {
    if(fd<0)return FenceState::Signaled;
    sync_file_info info;memset(&info,0,sizeof(info));
    if(ioctl(fd,SYNC_IOC_FILE_INFO,&info)==0)
        return info.status>0?FenceState::Signaled:info.status==0?FenceState::Pending:FenceState::Error;
    return fenceReady(fd)?FenceState::Signaled:FenceState::Pending;
}
// The same, with the injected faults: `readyValue` names a frame's ready
// fence, `leaseOrdinal` (non-zero) a read-lease completion fence. An injected
// error applies once the real fence has completed, as a reset's would.
inline FenceState fenceState(int fd,uint64_t readyValue,uint64_t leaseOrdinal) {
    FenceState s=fenceState(fd);
    const TestFaults& f=TestFaults::get();
    if(s==FenceState::Signaled && ((f.readyError && readyValue==f.readyError) ||
                                   (f.leaseError && leaseOrdinal==f.leaseError)))
        return FenceState::Error;
    return s;
}
struct SyncFds {
    int held=-1; // readiness of the acquired frame; pending ones live in PendingFrames
    std::deque<std::pair<uint64_t,int>> releases;
    ~SyncFds(){clear();}
    void clear(){if(held>=0)close(held);held=-1;
        for(auto r:releases)if(r.second>=0)close(r.second);releases.clear();}
    void advance(uint64_t& consumed){
        while(!releases.empty() && fenceReady(releases.front().second)) {
            auto r=releases.front();releases.pop_front();if(r.second>=0)close(r.second);
            if(r.first>consumed)consumed=r.first;
        }
    }
};
}
