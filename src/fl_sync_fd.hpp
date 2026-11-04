#pragma once
#include <poll.h>
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
