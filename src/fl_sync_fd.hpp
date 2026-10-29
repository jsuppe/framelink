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
    int pending=-1,held=-1;
    std::deque<std::pair<uint64_t,int>> releases;
    ~SyncFds(){clear();}
    void clear(){if(pending>=0)close(pending);if(held>=0)close(held);pending=held=-1;
        for(auto r:releases)if(r.second>=0)close(r.second);releases.clear();}
    void advance(uint64_t& consumed){
        while(!releases.empty() && fenceReady(releases.front().second)) {
            auto r=releases.front();releases.pop_front();if(r.second>=0)close(r.second);
            if(r.first>consumed)consumed=r.first;
        }
    }
};
}
