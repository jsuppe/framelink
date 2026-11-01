// Bounded, append-only per-thread flight recorder. No per-event I/O or lock.
// Reusable by clients: define FL_TRACE_NS and FL_TRACE_ENV before inclusion.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#ifndef FL_TRACE_NS
#define FL_TRACE_NS fltrace
#endif
#ifndef FL_TRACE_ENV
#define FL_TRACE_ENV "FRAMELINK_TRACE"
#endif
namespace FL_TRACE_NS {
inline bool enabled() noexcept {
#ifdef FL_TRACE_COMPILED_OUT
    return false;
#else
    static const bool on = [] { const char* p=std::getenv(FL_TRACE_ENV); return p && *p; }();
    return on;
#endif
}
inline uint64_t clockNs() noexcept {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline uint64_t id(const char* s) noexcept {
    if (!enabled() || !s) return 0;
    uint64_t h=14695981039346656037ull;
    while (*s) { h^=(unsigned char)*s++; h*=1099511628211ull; }
    return h;
}
// Detail mask distinguishes an observed zero from an unavailable field.
enum Detail : uint64_t { Payload=1, Generation=2, Occupancy=4, Token=8,
    Memory=16, Policy=32, Device=64 };
struct Event {
    const char* op; // static literal, never a temporary string
    uint64_t start, duration, object, sequence, fence, slot;
    int64_t pts, result;
    uint64_t details, bytes, generation, occupancy, token;
    uint64_t deviceLo, deviceHi;
    uint32_t resource, usage, operations, deviceKind;
    uint32_t missingUsage, unmetPreference, rejectedResource, forbiddenOperations;
    uint32_t endpoint, knownFields;
};
struct Buffer {
    std::unique_ptr<Event[]> events;
    const size_t capacity;
    std::atomic<size_t> published{0};
    std::atomic<uint64_t> dropped{0};
    explicit Buffer(size_t n):events(new Event[n]),capacity(n) {}
};
class Recorder {
    std::mutex mutex_;
    std::vector<std::unique_ptr<Buffer>> buffers_;
    std::atomic<uint64_t> rejected_{0};
    std::string path_;
    size_t capacity_=65536;
    uint64_t origin_=clockNs();
    uint64_t wall_=(uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
public:
    Recorder() {
        const char* p=std::getenv(FL_TRACE_ENV);
        path_=p?p:"";
        const std::string key=std::string(FL_TRACE_ENV)+"_EVENTS";
        if(const char* n=std::getenv(key.c_str())) {
            char* end=nullptr; unsigned long v=std::strtoul(n,&end,10);
            if(end!=n && !*end && v>=16 && v<=1048576) capacity_=v;
        }
    }
    Buffer* attach() noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(buffers_.size()>=64) { ++rejected_; return nullptr; }
            buffers_.emplace_back(new Buffer(capacity_));
            return buffers_.back().get();
        } catch (...) { ++rejected_; return nullptr; }
    }
    // Snapshot only committed, immutable records. Safe with active writers;
    // never reclaims/reset buffers. Call from a control thread, not a frame path.
    bool flush() noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
            const int pid=_getpid();
#else
            const int pid=getpid();
#endif
            const std::string file=path_+"."+std::to_string(pid)+".csv";
            FILE* f=std::fopen(file.c_str(),"w");
            if(!f) { std::fprintf(stderr,"trace: cannot open %s\n",file.c_str()); return false; }
            std::fprintf(f,"# schema=2,clock=steady_ns,pid=%d,origin_ns=%llu,wall_ns=%llu\n",pid,
                (unsigned long long)origin_,(unsigned long long)wall_);
            std::fprintf(f,"thread,operation,start_ns,duration_ns,channel_id,sequence,fence,slot,pts_ns,result,details,payload_bytes,generation,occupancy,token,device_lo,device_hi,resource_type,usage,operations,device_kind,missing_usage,unmet_preference,rejected_resource,forbidden_operations,endpoint,resource_known_fields\n");
            uint64_t dropped=0; size_t tid=0;
            for(auto& b:buffers_) {
                const size_t count=b->published.load(std::memory_order_acquire);
                for(size_t i=0;i<count;i++) {
                    const auto& e=b->events[i];
                    std::fprintf(f,"%zu,%s,%llu,%llu,%llu,%llu,%llu,%llu,%lld,%lld,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",tid,e.op,
                        (unsigned long long)e.start,(unsigned long long)e.duration,
                        (unsigned long long)e.object,(unsigned long long)e.sequence,
                        (unsigned long long)e.fence,(unsigned long long)e.slot,
                        (long long)e.pts,(long long)e.result,
                        (unsigned long long)e.details,(unsigned long long)e.bytes,
                        (unsigned long long)e.generation,(unsigned long long)e.occupancy,
                        (unsigned long long)e.token,(unsigned long long)e.deviceLo,
                        (unsigned long long)e.deviceHi,e.resource,e.usage,e.operations,e.deviceKind,
                        e.missingUsage,e.unmetPreference,e.rejectedResource,e.forbiddenOperations,e.endpoint,e.knownFields);
                }
                dropped+=b->dropped.load(std::memory_order_relaxed); ++tid;
            }
            std::fprintf(f,"# dropped_events=%llu,rejected_threads=%llu,capacity_per_thread=%zu\n",
                (unsigned long long)dropped,(unsigned long long)rejected_.load(),capacity_);
            const bool ok=!std::ferror(f); return std::fclose(f)==0 && ok;
        } catch(...) { return false; }
    }
    ~Recorder() { flush(); }
};
inline Recorder& recorder() { static Recorder value; return value; }
inline Buffer* buffer() noexcept {
    thread_local Buffer* value=[]() noexcept -> Buffer* {
        try { return recorder().attach(); } catch(...) { return nullptr; }
    }();
    return value;
}
inline void record(const Event& e) noexcept {
    if(!enabled()) return;
    Buffer* b=buffer(); if(!b) return;
    const size_t n=b->published.load(std::memory_order_relaxed);
    if(n==b->capacity) { b->dropped.fetch_add(1,std::memory_order_relaxed); return; }
    b->events[n]=e;
    b->published.store(n+1,std::memory_order_release);
}
// Observed logical payload work, not a declaration or driver-wide copy proof.
// GPU events mean the copy command was issued, not that GPU completion occurred.
inline void payload(const char* op,uint64_t object,uint64_t bytes,uint32_t operations) noexcept {
    if(!enabled())return;
    Event e{};e.op=op;e.object=object;e.start=clockNs();
    e.details=Payload;e.bytes=bytes;e.operations=operations;record(e);
}
struct Scope {
    Event event{};
    bool active;
    explicit Scope(const char* op,uint64_t object=0) noexcept:active(enabled()) {
        if(active) { event.op=op; event.object=object; event.start=clockNs(); event.result=-9999; }
    }
    template<class T> T result(T value) noexcept { if(active)event.result=(int64_t)value; return value; }
    ~Scope() { if(active) {event.duration=clockNs()-event.start;record(event);} }
};
// CPU call duration plus its return code. Does NOT imply GPU completion.
template<class F> auto call(const char* op,F&& fn) -> decltype(fn()) {
    Scope scope(op); return scope.result(fn());
}
inline bool flush() noexcept {
    try { return !enabled() || recorder().flush(); } catch(...) { return false; }
}
} // namespace
