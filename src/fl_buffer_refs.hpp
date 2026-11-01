#pragma once
#include "framelink.h"
#include <array>
#include <atomic>
#include <mutex>
namespace fl {
// Producer-local references complement (never replace) remote completion.
// Registration follows serialized producer calls. Completion may run on a
// GstMemory finalizer thread. Bounded storage; no allocation on the frame path.
class BufferReferences {
    struct Entry { uint64_t token=0; uint32_t slot=0; };
    std::array<Entry,256> entries_{};
    std::array<std::atomic<uint32_t>,FL_MAX_POOL_SIZE> counts_{};
    std::atomic<uint32_t> total_{0};
    std::mutex mutex_;
    inline static std::atomic<uint64_t> next_{1};
public:
    bool busy(uint32_t slot) const { return slot<FL_MAX_POOL_SIZE && counts_[slot].load(std::memory_order_acquire)!=0; }
    bool empty() const { return total_.load(std::memory_order_acquire)==0; }
    flResult retain(uint32_t slot,uint64_t& out) {
        if(slot>=FL_MAX_POOL_SIZE)return FL_INVALID;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& e:entries_)if(!e.token){
            auto token=next_.load(std::memory_order_relaxed);
            do {if(token==UINT64_MAX)return FL_INTERNAL;}
            while(!next_.compare_exchange_weak(token,token+1,std::memory_order_relaxed));
            e={token,slot};counts_[slot].fetch_add(1,std::memory_order_release);
            total_.fetch_add(1,std::memory_order_release);out=token;return FL_OK;
        }
        return FL_BUSY;
    }
    flResult release(uint64_t token) {
        if(!token)return FL_INVALID;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& e:entries_)if(e.token==token){
            auto slot=e.slot;e={};counts_[slot].fetch_sub(1,std::memory_order_release);
            total_.fetch_sub(1,std::memory_order_release);return FL_OK;
        }
        return FL_INVALID;
    }
};
}
