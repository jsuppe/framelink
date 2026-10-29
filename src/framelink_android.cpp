#include "framelink_trace.hpp"
// framelink Android backend. See framelink.h.
//
// The consumer allocates the ring as AHardwareBuffers and hands each one to
// the producer with AHardwareBuffer_sendHandleToUnixSocket - Android's blessed
// way to move a buffer between processes without Binder. Nothing is copied
// afterwards: the buffer the producer draws into is the exact allocation the
// consumer samples, the same guarantee every other backend makes.
//
// DIFFERENCES FROM THE LINUX BACKEND, all platform-driven:
//
// 1. Native AHB formats differ from public CPU views. BGRA maps through an
//    RGBA swizzle and NV12 through YUV420 plane normalization. GPU callers
//    must query the AHB native/external format rather than assuming bytes.
//
// 2. flImageMap/flImageUnmap are AHardwareBuffer_lock/unlock, and the unlock
//    is a CACHE BOUNDARY: CPU writes are not guaranteed visible to the GPU
//    until the buffer is unlocked. flSubmit/flEndSubmit therefore unmap
//    automatically if the producer left the buffer mapped - forgetting the
//    unlock must cost nothing, because the failure it causes (stale tiles on
//    some devices, none on others) is the worst kind to debug.
//
// 3. The producer is SINGLE-READER by construction. The Linux backend runs a
//    background thread for FrameReleased messages; here that thread would
//    race AHardwareBuffer_recvHandleFromUnixSocket - which does its own
//    recvmsg - and eat buffer handles off the socket. All producer-side
//    receives happen on the caller's thread instead: flAcquireBuffer drains
//    release messages non-blocking, flRequestGeometry reads until its RingDesc
//    arrives, processing releases inline. One channel, one calling thread -
//    which is how every producer is written anyway.
//
// Sync matches Linux: completion FDs via framelink_sync_fd.h. flSubmit means "I have finished
// writing" - true for a CPU producer (the auto-unlock flushes), and a GPU
// producer must have flushed. Slot recycling is monotonic (value V frees every
// submit <= V), the lesson the Linux backend learned from a bursty decoder.

#define FL_EXPORTS
#include "framelink.h"
#include "framelink_sync_fd.h"
#include "fl_sync_fd.hpp"
#include "framelink_ahb.h"

#ifdef __ANDROID__

#include <android/hardware_buffer.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fl_ipc.h"
#include "fl_log.h"
#include "fl_protocol.h"

struct flImage {
    AHardwareBuffer* ahb = nullptr;
    void* mapped = nullptr;
    void* nativeMapped = nullptr;
    uint64_t strideBytes = 0;
    flFormat format = FL_FORMAT_RGBA8;
    AHardwareBuffer_Planes lockedPlanes{};
    std::vector<uint8_t> nv12;
    bool nativeNv12 = false;
    uint32_t width = 0, height = 0;
};

struct flChannel {
    bool isConsumer = false;
    std::string name;
    flChannelInfo info{};
    uint32_t flags = 0;

    std::vector<flImage*> images;
    fl::SyncFds sync;
    uint64_t readyValue = 0;

    // consumer
    fl::Channel peer;
    std::thread accept;
    std::atomic<bool> stop{false};
    std::atomic<bool> peerGone{false};
    std::mutex pendingMutex;
    bool hasPending = false;
    bool frameHeld = false;
    uint64_t heldReady = 0;
    uint64_t retiredValue = 0; // completed consumer GPU reads; protects geometry changes
    uint32_t heldSlot = 0;
    uint32_t pendingSlot = 0;
    uint64_t pendingReady = 0;
    int64_t pendingPts = -1;
    uint64_t sequence = 0;

    // producer (single-reader: no mutex needed, one calling thread by contract)
    fl::Channel link;
    std::vector<uint64_t> slotSubmitValue; // last submit counter per slot
    uint64_t consumedValue = 0;            // everything <= this is free
};

namespace {

uint32_t ahbFormatFor(flFormat f) {
    return (f == FL_FORMAT_RGBA8 || f == FL_FORMAT_BGRA8) ? AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM :
           f == FL_FORMAT_RGB10A2 ? AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM :
           f == FL_FORMAT_NV12 ? AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420 : 0;
}

void freeImage(flImage* img) {
    flImageUnmap(img);
    if (img->ahb) AHardwareBuffer_release(img->ahb);
    delete img;
}

// Allocate (or re-allocate) the ring.
bool reallocRing(flChannel* ch, uint32_t w, uint32_t h, uint32_t poolSize) {
    std::vector<flImage*> fresh; // preserve the old ring if allocation fails
    AHardwareBuffer_Desc desc{};
    desc.width = w;
    desc.height = h;
    desc.layers = 1;
    desc.format = ahbFormatFor(ch->info.format);
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (ch->flags & FL_MAP_CPU)
        desc.usage |= AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_CPU_READ_RARELY;
    for (uint32_t i = 0; i < poolSize; ++i) {
        auto* img = new flImage();
        if (AHardwareBuffer_allocate(&desc, &img->ahb) != 0 || !img->ahb) {
            delete img;
            for(auto* old:fresh)freeImage(old);
            return false;
        }
        AHardwareBuffer_Desc got{};
        AHardwareBuffer_describe(img->ahb, &got);
        img->format = ch->info.format;
        img->width = w; img->height = h;
        img->strideBytes = img->format == FL_FORMAT_NV12 ? w : (uint64_t)got.stride * 4;
        fresh.push_back(img);
    }
    for(auto* old:ch->images)freeImage(old);
    ch->images=std::move(fresh);
    ch->info.width=w;ch->info.height=h;++ch->info.generation;
    ch->heldReady=ch->retiredValue=0;ch->sync.clear();
    return true;
}

// Hand the current ring to a producer: the RingDesc first (payload only), then
// each buffer through the platform's own handle-passing call. The stride
// travels in the desc so flImageMap agrees on both sides without a re-describe.
bool handOverRing(flChannel* ch, fl::Channel& link) {
    fl::RingDescMsg rd{};
    rd.generation = ch->info.generation;
    rd.width = ch->info.width;
    rd.height = ch->info.height;
    rd.format = (uint32_t)ch->info.format;
    rd.poolSize = (uint32_t)ch->images.size();
    for (size_t i = 0; i < ch->images.size(); ++i) rd.stride[i] = ch->images[i]->strideBytes;
    if (!link.send(fl::MsgType::RingDesc, rd)) return false;
    for (flImage* img : ch->images)
        if (AHardwareBuffer_sendHandleToUnixSocket(img->ahb, link.pollFd()) != 0) {
            FL_LOG_ERR("framelink: AHardwareBuffer_sendHandleToUnixSocket failed");
            return false;
        }
    return true;
}

// Producer: adopt a ring - the counterpart of handOverRing, on the SAME thread
// that received the RingDesc (single-reader; see the file header).
flResult importRing(flChannel* ch, const fl::RingDescMsg* rd) {
    if(rd->generation==ch->info.generation && !ch->images.empty()) {
        // Refused resize: drain duplicated native handles, retain slot/fence
        // counters and outstanding ownership rather than resetting the ring.
        for(uint32_t i=0;i<rd->poolSize;i++) {
            AHardwareBuffer* duplicate=nullptr;
            if(AHardwareBuffer_recvHandleFromUnixSocket(ch->link.pollFd(),&duplicate)!=0 || !duplicate)
                return FL_DISCONNECTED;
            AHardwareBuffer_release(duplicate);
        }
        return FL_OK;
    }
    ch->sync.clear();
    for (flImage* img : ch->images) freeImage(img);
    ch->images.clear();
    for (uint32_t i = 0; i < rd->poolSize; ++i) {
        auto* img = new flImage();
        if (AHardwareBuffer_recvHandleFromUnixSocket(ch->link.pollFd(), &img->ahb) != 0 ||
            !img->ahb) {
            delete img;
            FL_LOG_ERR("framelink: AHardwareBuffer_recvHandleFromUnixSocket failed");
            return FL_DISCONNECTED;
        }
        img->strideBytes = rd->stride[i];
        img->format = (flFormat)rd->format;
        img->width = rd->width; img->height = rd->height;
        ch->images.push_back(img);
    }
    ch->slotSubmitValue.assign(rd->poolSize, 0);
    ch->consumedValue = 0;
    ch->readyValue = 0;
    ch->info.generation = rd->generation;
    ch->info.width = rd->width;
    ch->info.height = rd->height;
    ch->info.format = (flFormat)rd->format;
    ch->info.poolSize = rd->poolSize;
    return FL_OK;
}

// Producer: apply one already-received control message. Called from whichever
// producer entry point happened to be reading the socket.
void applyRelease(flChannel* ch, const fl::Message& msg) {
    if (msg.type != fl::MsgType::FrameReleased) return;
    const auto* fr = msg.as<fl::FrameReadyMsg>();
    if (!fr) return;
    // Monotonic: releasing value V frees the skipped frames before V too.
    ch->sync.releases.push_back({fr->readyValue,msg.fds.empty()?-1:msg.fds[0]});
    for(size_t i=1;i<msg.fds.size();i++)close(msg.fds[i]);
    ch->sync.advance(ch->consumedValue);
}

// Producer: drain any queued control messages without blocking.
void drainProducerMessages(flChannel* ch) {
    for (;;) {
        pollfd pfd{ch->link.pollFd(), POLLIN, 0};
        if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & (POLLIN | POLLHUP | POLLERR))) return;
        auto msg = ch->link.recv();
        if (!msg) return;
        applyRelease(ch, *msg);
    }
}

void acceptLoop(flChannel* ch) {
    // One listener for the channel's whole life - FL_BUSY depends on it (the
    // Linux backend's lesson; see fl::Listener).
    fl::Listener lst = fl::Listener::create(ch->name);
    if (!lst.valid()) {
        FL_LOG_ERR("framelink: cannot listen on '%s'", ch->name.c_str());
        return;
    }
    while (!ch->stop.load()) {
        fl::Channel link = lst.accept();
        if (ch->stop.load()) return;
        if (!link.connected()) continue;

        fl::HelloMsg hello{};
        hello.version = FL_VERSION;
        hello.pid = (uint32_t)getpid();
        if (!link.send(fl::MsgType::Hello, hello)) continue;
        auto peer = link.recv();
        const auto* ph = peer ? peer->as<fl::HelloMsg>() : nullptr;
        if (!ph || ph->version != FL_VERSION) {
            FL_LOG_ERR("framelink: refusing producer - speaks version %u, we speak %u",
                       ph ? ph->version : 0u, (unsigned)FL_VERSION);
            continue;
        }

        if (!handOverRing(ch, link)) continue;

        ch->peerGone = false;
        FL_LOG("framelink: producer pid %u attached to '%s'", link.peerPid(), ch->name.c_str());
        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            ch->peer = std::move(link);
        }

        for (;;) {
            // The producer AND the listener: extras get Busy NOW, not when the
            // current producer next sends.
            pollfd fds[2] = {{ch->peer.pollFd(), POLLIN, 0}, {lst.pollFd(), POLLIN, 0}};
            if (poll(fds, 2, -1) < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ch->stop.load()) return;
            if (fds[1].revents & POLLIN) {
                fl::Channel extra = lst.accept();
                if (extra.connected()) extra.send(fl::MsgType::Busy);
            }
            if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            auto msg = ch->peer.recv();
            if (!msg || msg->type == fl::MsgType::Bye) break;
            if (msg->type == fl::MsgType::RequestGeometry) {
                const auto* g = msg->as<fl::GeometryMsg>();
                std::lock_guard<std::mutex> geometryLock(ch->pendingMutex);
                ch->sync.advance(ch->retiredValue);
                if (ch->frameHeld || ch->retiredValue < ch->heldReady) { if (!handOverRing(ch, ch->peer)) break; continue; }
                if (g && g->width && g->height && g->width <= 8192 && g->height <= 8192 &&
                    (g->format == (uint32_t)ch->info.format) &&
                    (ch->info.format != FL_FORMAT_NV12 || !((g->width | g->height) & 1)) &&
                    (g->width != ch->info.width || g->height != ch->info.height)) {
                    ch->hasPending = false; // stale: the old ring is going away
                    if(ch->sync.pending>=0)close(ch->sync.pending);ch->sync.pending=-1;
                    const uint32_t pool = (uint32_t)ch->images.size();
                    if (!reallocRing(ch, g->width, g->height, pool)) {
                        FL_LOG_ERR("framelink: reallocation to %ux%u failed", g->width,
                                   g->height);
                        if(!handOverRing(ch,ch->peer))break;
                        continue;
                    }
                    FL_LOG("framelink: '%s' reallocated to %ux%u at the producer's request",
                           ch->name.c_str(), g->width, g->height);
                }
                if (!handOverRing(ch, ch->peer)) break;
                continue;
            }
            if (msg->type != fl::MsgType::FrameReady) continue;
            const auto* fr = msg->as<fl::FrameReadyMsg>();
            if (!fr || fr->slot >= ch->images.size()) continue;
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            // Latest-frame-wins: a slow consumer must not stall the producer.
            if(fltrace::enabled())fltrace::record({"ipc.frame_received",fltrace::clockNs(),0,
                fltrace::id(ch->name.c_str()),ch->sequence+1,fr->readyValue,fr->slot,fr->ptsNs,ch->hasPending?1:0});
            if(ch->sync.pending>=0)close(ch->sync.pending);
            ch->sync.pending=msg->fds.empty()?-1:msg->fds[0];
            for(size_t i=1;i<msg->fds.size();i++)close(msg->fds[i]);
            ch->hasPending = true;
            ch->pendingSlot = fr->slot;
            ch->pendingReady = fr->readyValue;
            ch->pendingPts = fr->ptsNs;
            ++ch->sequence;
        }
        ch->peerGone = true;
        if (ch->stop.load()) return;
    }
}

} // namespace

extern "C" {

FL_API const char* flResultString(flResult r) {
    switch (r) {
        case FL_OK: return "ok";
        case FL_TIMEOUT: return "timeout";
        case FL_DISCONNECTED: return "peer disconnected";
        case FL_VERSION_MISMATCH: return "protocol version mismatch";
        case FL_FORMAT_UNSUPPORTED: return "format unsupported";
        case FL_ADAPTER_MISMATCH: return "producer and consumer on different GPUs";
        case FL_ACCESS_DENIED: return "access denied";
        case FL_NOT_FOUND: return "channel not found";
        case FL_INVALID: return "invalid argument";
        case FL_OOM: return "out of memory";
        case FL_INTERNAL: return "internal error";
        case FL_BUSY: return "channel already has a producer";
    }
    return "unknown";
}

// One GPU on a phone; the LUID means nothing here.
FL_API flResult flCreateChannelOnAdapter(const char* name, uint32_t width, uint32_t height,
                                         flFormat format, uint32_t poolSize, uint32_t flags,
                                         const uint8_t*, flChannel** out) {
    fltrace::Scope trace("flCreateChannelOnAdapter",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=format;trace.event.slot=poolSize;trace.event.pts=flags;}

    return trace.result(flCreateChannelEx(name, width, height, format, poolSize, flags, out));
}

FL_API flResult flCreateChannelEx(const char* name, uint32_t width, uint32_t height,
                                  flFormat format, uint32_t poolSize, uint32_t flags,
                                  flChannel** out) {
    fltrace::Scope trace("flCreateChannelEx",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=format;trace.event.slot=poolSize;trace.event.pts=flags;}

    if (!name || !out || !width || !height || width>8192 || height>8192) return trace.result(FL_INVALID);
    if (!ahbFormatFor(format)) return trace.result(FL_FORMAT_UNSUPPORTED); // unsupported public format
    if (format == FL_FORMAT_NV12 && ((width | height) & 1)) return trace.result(FL_INVALID);
    if (!poolSize || poolSize > fl::kMaxPool) return trace.result(FL_INVALID);

    auto* ch = new flChannel();
    ch->isConsumer = true;
    ch->name = name;
    ch->flags = flags;
    ch->info.version = FL_VERSION;
    ch->info.width = width;
    ch->info.height = height;
    ch->info.format = format;
    ch->info.poolSize = poolSize;

    if (!reallocRing(ch, width, height, poolSize)) {
        flClose(ch);
        return trace.result(FL_OOM);
    }

    ch->accept = std::thread(acceptLoop, ch);
    *out = ch;
    return trace.result(FL_OK);
}

FL_API flResult flCreateChannel(const char* name, uint32_t width, uint32_t height,
                                flFormat format, uint32_t poolSize, flChannel** out) {
    return flCreateChannelEx(name, width, height, format, poolSize, 0, out);
}

FL_API flResult flOpenProducer(const char* name, flChannel** out) {
    fltrace::Scope trace("flOpenProducer",fltrace::id(name));

    if (!name || !out) return trace.result(FL_INVALID);
    auto* ch = new flChannel();
    ch->name = name;
    ch->link = fl::Channel::connect(name, 300);
    if (!ch->link.connected()) {
        delete ch;
        return trace.result(FL_NOT_FOUND);
    }

    auto helloMsg = ch->link.recv();
    if (helloMsg && helloMsg->type == fl::MsgType::Busy) {
        delete ch;
        return trace.result(FL_BUSY);
    }
    const auto* srv = helloMsg && helloMsg->type == fl::MsgType::Hello
                          ? helloMsg->as<fl::HelloMsg>()
                          : nullptr;
    if (!srv) {
        delete ch;
        return trace.result(FL_DISCONNECTED);
    }
    if (srv->version != FL_VERSION) {
        FL_LOG_ERR("framelink: channel '%s' speaks version %u, we speak %u", name,
                   srv->version, (unsigned)FL_VERSION);
        delete ch;
        return trace.result(FL_VERSION_MISMATCH);
    }
    fl::HelloMsg mine{};
    mine.version = FL_VERSION;
    mine.pid = (uint32_t)getpid();
    if (!ch->link.send(fl::MsgType::Hello, mine)) {
        delete ch;
        return trace.result(FL_INTERNAL);
    }

    auto ring = ch->link.recv();
    const auto* rd = ring && ring->type == fl::MsgType::RingDesc
                         ? ring->as<fl::RingDescMsg>()
                         : nullptr;
    if (!rd || rd->poolSize == 0 || rd->poolSize > fl::kMaxPool) {
        delete ch;
        return trace.result(FL_DISCONNECTED);
    }
    ch->info.version = FL_VERSION;
    if (importRing(ch, rd) != FL_OK) {
        flClose(ch);
        return trace.result(FL_DISCONNECTED);
    }
    *out = ch;
    return trace.result(FL_OK);
}

FL_API flResult flRequestGeometry(flChannel* ch, uint32_t width, uint32_t height,
                                  flFormat format) {
    fltrace::Scope trace("flRequestGeometry",fltrace::id(ch?ch->name.c_str():nullptr));

    if (!ch || ch->isConsumer || !width || !height) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    fl::GeometryMsg g{};
    g.width = width;
    g.height = height;
    g.format = (uint32_t)format;
    if (!ch->link.send(fl::MsgType::RequestGeometry, g)) return trace.result(FL_DISCONNECTED);
    // The consumer always answers with a RingDesc - the current ring if it
    // declined. We are the only reader (see the file header), so anything else
    // arriving first is a release to apply inline, never a message to lose.
    for (;;) {
        auto reply = ch->link.recv();
        if (!reply) return trace.result(FL_DISCONNECTED);
        if (reply->type == fl::MsgType::FrameReleased) {
            applyRelease(ch, *reply);
            continue;
        }
        const auto* rd = reply->type == fl::MsgType::RingDesc ? reply->as<fl::RingDescMsg>()
                                                              : nullptr;
        if (!rd || rd->poolSize == 0 || rd->poolSize > fl::kMaxPool) return trace.result(FL_DISCONNECTED);
        return trace.result(importRing(ch, rd));
    }
}

FL_API flResult flAcquireBuffer(flChannel* ch, flBuffer* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireBuffer",fltrace::id(ch?ch->name.c_str():nullptr));

    if (!ch || ch->isConsumer || !out) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    for (uint32_t waited = 0;; waited += 2) {
        drainProducerMessages(ch);
        if(!ch->link.connected())return trace.result(FL_DISCONNECTED);
        ch->sync.advance(ch->consumedValue);
        for (size_t i = 0; i < ch->images.size(); ++i) {
            if (ch->slotSubmitValue[i] <= ch->consumedValue) {
                // Pin until flEndSubmit records the real value (the sentinel is
                // unreachable by the consumer - it can only release submitted
                // values). Acquiring and never submitting leaks the slot.
                ch->slotSubmitValue[i] = UINT64_MAX;
                out->slot = (uint32_t)i;
                out->image = ch->images[i];
                if(trace.active){trace.event.slot=out->slot;}
                return trace.result(FL_OK);
            }
        }
        if (waited >= timeoutMs) return trace.result(FL_TIMEOUT);
        usleep(2000);
    }
}

FL_API flResult flBeginSubmit(flChannel* ch, const flBuffer* buf, uint64_t* signalValue) {
    fltrace::Scope trace("flBeginSubmit",fltrace::id(ch?ch->name.c_str():nullptr));

    if (!ch || ch->isConsumer || !buf || !signalValue) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    *signalValue = ++ch->readyValue;
    if(trace.active)trace.event.fence=*signalValue;
    return trace.result(FL_OK);
}

FL_API flResult flEndSubmit(flChannel* ch, const flBuffer* buf, uint64_t signalValue,
                            int64_t ptsNs) {
    fltrace::Scope trace("flEndSubmit",fltrace::id(ch?ch->name.c_str():nullptr));
    if(trace.active && buf){trace.event.slot=buf->slot;trace.event.pts=ptsNs;}
    if(trace.active)trace.event.fence=signalValue;

    if (!ch || ch->isConsumer || !buf || buf->slot >= ch->images.size()) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    // The cache boundary: a mapped buffer's CPU writes are not GPU-visible
    // until unlocked. Automatic, so forgetting cannot produce the
    // works-on-one-device failure mode.
    flImageUnmap(buf->image);
    ch->slotSubmitValue[buf->slot] = signalValue;
    fl::FrameReadyMsg fr{};
    fr.slot = buf->slot;
    fr.readyValue = signalValue;
    fr.ptsNs = ptsNs;
    return trace.result(ch->link.send(fl::MsgType::FrameReady, fr) ? FL_OK : FL_DISCONNECTED);
}

FL_API flResult flSubmit(flChannel* ch, const flBuffer* buf, int64_t ptsNs) {
    fltrace::Scope trace("flSubmit",fltrace::id(ch?ch->name.c_str():nullptr));
    if(trace.active && buf){trace.event.slot=buf->slot;trace.event.pts=ptsNs;}

    uint64_t v = 0;
    flResult r = flBeginSubmit(ch, buf, &v);
    return trace.result(r == FL_OK ? flEndSubmit(ch, buf, v, ptsNs) : r);
}

FL_API flResult flSharedConsumedFence(flChannel*, void**) {
    return FL_FORMAT_UNSUPPORTED; // use framelink_sync_fd.h for native completion FDs
}

FL_API flResult flSharedReadyFence(flChannel*, void**) {
    return FL_FORMAT_UNSUPPORTED; // use framelink_sync_fd.h
}

FL_API flResult flAcquireFrame(flChannel* ch, flFrame* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireFrame",fltrace::id(ch?ch->name.c_str():nullptr));

    if (!ch || !ch->isConsumer || !out) return trace.result(FL_INVALID);
    for (uint32_t waited = 0;; waited += 2) {
        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            if (ch->hasPending && !ch->frameHeld &&
                ((ch->flags & FL_GPU_SYNC) || fl::fenceReady(ch->sync.pending))) {
                ch->sync.held=ch->sync.pending;ch->sync.pending=-1;
                ch->frameHeld = true;
                ch->heldReady = ch->pendingReady;
                ch->heldSlot = ch->pendingSlot;
                ch->hasPending = false;
                out->slot = ch->pendingSlot;
                out->image = ch->images[ch->pendingSlot];
                out->ptsNs = ch->pendingPts;
                out->sequence = ch->sequence;
                out->_readyValue = ch->pendingReady;
                if(trace.active){trace.event.slot=out->slot;trace.event.sequence=out->sequence;trace.event.fence=out->_readyValue;trace.event.pts=out->ptsNs;}
                return trace.result(FL_OK);
            }
        }
        if (ch->peerGone.load()) return trace.result(FL_DISCONNECTED);
        if (waited >= timeoutMs) return trace.result(FL_TIMEOUT);
        usleep(2000);
    }
}

FL_API flResult flReleaseSyncFd(flChannel* ch, const flFrame* frame, int consumedFd) {
    fltrace::Scope trace("flRelease",fltrace::id(ch?ch->name.c_str():nullptr));
    if(trace.active && frame){trace.event.sequence=frame->sequence;trace.event.fence=frame->_readyValue;trace.event.slot=frame->slot;trace.event.pts=frame->ptsNs;}

    if (!ch || !ch->isConsumer || !frame || frame->slot >= ch->images.size()) return trace.result(FL_INVALID);
    fl::FrameReadyMsg fr{};
    fr.slot = frame->slot;
    fr.readyValue = frame->_readyValue;
    std::lock_guard<std::mutex> hold(ch->pendingMutex);
    if (!ch->frameHeld || ch->heldReady != frame->_readyValue || ch->heldSlot != frame->slot)
        return trace.result(FL_INVALID);
    int localFd=consumedFd>=0?dup(consumedFd):-1;
    if(consumedFd>=0 && localFd<0)return trace.result(FL_INTERNAL);
    ch->sync.releases.push_back({frame->_readyValue,localFd});
    ch->sync.advance(ch->retiredValue);
    ch->frameHeld = false;
    if(ch->sync.held>=0)close(ch->sync.held);ch->sync.held=-1;
    if (!ch->peer.connected()) return trace.result(FL_OK);
    return trace.result(ch->peer.sendWithFds(fl::MsgType::FrameReleased, &fr, sizeof(fr), consumedFd>=0?&consumedFd:nullptr, consumedFd>=0?1:0) ? FL_OK : FL_DISCONNECTED);
}

FL_API flResult flRelease(flChannel* ch, const flFrame* frame) {
    if(ch && (ch->flags & (FL_GPU_SYNC|FL_DEFER_RELEASE))) {
        if(!ch->isConsumer || !frame)return FL_INVALID;
        std::lock_guard<std::mutex> hold(ch->pendingMutex);
        if(!ch->frameHeld || frame->_readyValue!=ch->heldReady || frame->slot!=ch->heldSlot)return FL_INVALID;
        ch->frameHeld=false;
        if(ch->sync.held>=0)close(ch->sync.held);ch->sync.held=-1;
        return FL_OK;
    }
    return flReleaseSyncFd(ch,frame,-1);
}
FL_API flResult flSignalConsumedSyncFd(flChannel* ch,uint64_t value,int fd) {
    if(!ch || !ch->isConsumer || !(ch->flags & (FL_GPU_SYNC|FL_DEFER_RELEASE)))return FL_INVALID;
    std::lock_guard<std::mutex> hold(ch->pendingMutex);
    if(value>ch->heldReady)return FL_INVALID;
    int localFd=fd>=0?dup(fd):-1;
    if(fd>=0 && localFd<0)return FL_INTERNAL;
    ch->sync.releases.push_back({value,localFd});
    ch->sync.advance(ch->retiredValue);
    fl::FrameReadyMsg message{};message.readyValue=value;
    return ch->peer.sendWithFds(fl::MsgType::FrameReleased,&message,sizeof(message),
        fd>=0?&fd:nullptr,fd>=0?1:0)?FL_OK:FL_DISCONNECTED;
}
FL_API flResult flFrameReadySyncFd(flChannel* ch,const flFrame* frame,int* fd) {
    if(!ch || !ch->isConsumer || !frame || !fd)return FL_INVALID;
    std::lock_guard<std::mutex> hold(ch->pendingMutex);
    if(!ch->frameHeld || frame->_readyValue!=ch->heldReady || frame->slot!=ch->heldSlot)return FL_INVALID;
    *fd=ch->sync.held;return FL_OK;
}
FL_API flResult flSubmitSyncFd(flChannel* ch,const flBuffer* buffer,int fd,int64_t pts) {
    fltrace::Scope trace("flSubmitSyncFd",fltrace::id(ch?ch->name.c_str():nullptr));
    if(trace.active){trace.event.pts=pts;trace.event.slot=buffer?buffer->slot:0;}
    if(!ch || ch->isConsumer || !buffer || buffer->slot>=ch->images.size() || buffer->image!=ch->images[buffer->slot])return FL_INVALID;
    if(!ch->link.connected())return FL_DISCONNECTED;
    flImageUnmap(buffer->image);
    uint64_t value=++ch->readyValue;ch->slotSubmitValue[buffer->slot]=value;
    if(trace.active)trace.event.fence=value;
    fl::FrameReadyMsg fr{};fr.slot=buffer->slot;fr.readyValue=value;fr.ptsNs=pts;
    return trace.result(ch->link.sendWithFds(fl::MsgType::FrameReady,&fr,sizeof(fr),fd>=0?&fd:nullptr,fd>=0?1:0)?FL_OK:FL_DISCONNECTED);
}

FL_API flResult flQuery(flChannel* ch, flChannelInfo* out) {
    if (!ch || !out) return FL_INVALID;
    *out = ch->info;
    return FL_OK;
}

FL_API void flClose(flChannel* ch) {
    fltrace::Scope trace("flClose",fltrace::id(ch?ch->name.c_str():nullptr));
    if (!ch) return;
    if (ch->isConsumer) {
        ch->stop = true;
        { fl::Channel poke = fl::Channel::connect(ch->name, 100); }
        if (ch->accept.joinable()) ch->accept.join();
    } else {
        if (ch->link.connected()) ch->link.send(fl::MsgType::Bye);
    }
    for (flImage* img : ch->images) freeImage(img);
    delete ch;
}

// ---- AHardwareBuffer interop -------------------------------------------------

FL_API AHardwareBuffer* flImageAHardwareBuffer(flImage* img) {
    return img ? img->ahb : nullptr;
}

FL_API flResult flImageMap(flImage* img, void** pixels, uint64_t* stride) {
    fltrace::Scope trace("flImageMap",(uint64_t)(uintptr_t)img);

    if (!img || !pixels) return trace.result(FL_INVALID);
    if (img->format == FL_FORMAT_NV12) return trace.result(FL_FORMAT_UNSUPPORTED);
    if (img->mapped) {
        *pixels = img->mapped;
        if (stride) *stride = img->strideBytes;
        return trace.result(FL_OK);
    }
    void* p = nullptr;
    if (AHardwareBuffer_lock(img->ahb,
                             AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                                 AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,
                             -1, nullptr, &p) != 0 ||
        !p)
        return trace.result(FL_FORMAT_UNSUPPORTED); // not allocated with FL_MAP_CPU
    if (img->format == FL_FORMAT_BGRA8) {
        try { img->nv12.resize((size_t)img->strideBytes * img->height); }
        catch (...) { AHardwareBuffer_unlock(img->ahb, nullptr); return trace.result(FL_OOM); }
        img->nativeMapped = p;
        auto* dst = img->nv12.data();
        for (unsigned y = 0; y < img->height; ++y)
            for (unsigned x = 0; x < img->width; ++x) {
                size_t i = y * img->strideBytes + x * 4;
                const auto* src = (const uint8_t*)p;
                dst[i] = src[i+2]; dst[i+1] = src[i+1]; dst[i+2] = src[i]; dst[i+3] = src[i+3];
            }
        p = dst;
    }
    img->mapped = p;
    *pixels = p;
    if (stride) *stride = img->strideBytes;
    return trace.result(FL_OK);
}

FL_API flResult flImageMapPlanes(flImage* img, flMappedImage* out) {
    fltrace::Scope trace("flImageMapPlanes", (uint64_t)(uintptr_t)img);
    if (!img || !out) return trace.result(FL_INVALID);
    *out = {};
    if (img->format == FL_FORMAT_NV12) {
        // Android YUV420 may be planar or NV21. Normalize the public CPU view
        // to NV12; the AHB interop handle retains its native external format.
        if (!img->mapped) {
            try { img->nv12.resize((size_t)img->width * img->height * 3 / 2); }
            catch (...) { return trace.result(FL_OOM); }
            if (AHardwareBuffer_lockPlanes(img->ahb,
                    AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,
                    -1, nullptr, &img->lockedPlanes) != 0) return trace.result(FL_FORMAT_UNSUPPORTED);
            if (img->lockedPlanes.planeCount != 3) {
                AHardwareBuffer_unlock(img->ahb, nullptr);
                return trace.result(FL_FORMAT_UNSUPPORTED);
            }
            const auto& yPlane = img->lockedPlanes.planes[0];
            const auto& uPlane = img->lockedPlanes.planes[1];
            const auto& vPlane = img->lockedPlanes.planes[2];
            img->nativeNv12 = yPlane.pixelStride == 1 && uPlane.pixelStride == 2 &&
                vPlane.pixelStride == 2 && uPlane.rowStride == vPlane.rowStride &&
                (uintptr_t)vPlane.data == (uintptr_t)uPlane.data + 1;
            img->mapped = img->nativeNv12 ? yPlane.data : img->nv12.data();
            if (!img->nativeNv12) for (unsigned p = 0; p < 3; ++p) {
                const auto& plane = img->lockedPlanes.planes[p];
                auto* dst = img->nv12.data() + (p ? (size_t)img->width * img->height + p - 1 : 0);
                for (unsigned y = 0; y < (p ? img->height / 2 : img->height); ++y)
                    for (unsigned x = 0; x < (p ? img->width / 2 : img->width); ++x)
                        dst[(size_t)y * img->width + x * (p ? 2 : 1)] =
                            ((const uint8_t*)plane.data)[(size_t)y * plane.rowStride + x * plane.pixelStride];
            }
        }
        out->planes = 2;
        if (img->nativeNv12) {
            out->plane[0] = {img->lockedPlanes.planes[0].data, img->lockedPlanes.planes[0].rowStride,
                            img->width, img->height};
            out->plane[1] = {img->lockedPlanes.planes[1].data, img->lockedPlanes.planes[1].rowStride,
                            img->width, img->height / 2};
            return trace.result(FL_OK);
        }
        out->plane[0] = {img->mapped, img->width, img->width, img->height};
        out->plane[1] = {img->nv12.data() + (size_t)img->width * img->height,
                        img->width, img->width, img->height / 2};
        return trace.result(FL_OK);
    }
    void* pixels = nullptr;
    uint64_t stride = 0;
    flResult r = flImageMap(img, &pixels, &stride);
    if (r != FL_OK) return trace.result(r);
    out->planes = 1;
    out->plane[0] = {pixels, stride, img->width * 4, img->height};
    return trace.result(FL_OK);
}

FL_API void flImageUnmap(flImage* img) {
    fltrace::Scope trace("flImageUnmap",(uint64_t)(uintptr_t)img);
    if (!img || !img->mapped) return;
    if (img->format == FL_FORMAT_BGRA8) {
        auto* dst = (uint8_t*)img->nativeMapped;
        const auto* src = (const uint8_t*)img->mapped;
        for (unsigned y = 0; y < img->height; ++y)
            for (unsigned x = 0; x < img->width; ++x) {
                size_t i = y * img->strideBytes + x * 4;
                dst[i] = src[i+2]; dst[i+1] = src[i+1]; dst[i+2] = src[i]; dst[i+3] = src[i+3];
            }
        img->nativeMapped = nullptr;
    }
    if (img->format == FL_FORMAT_NV12 && !img->nativeNv12) {
        for (unsigned p = 0; p < 3; ++p) {
            const auto& plane = img->lockedPlanes.planes[p];
            const auto* src = img->nv12.data() + (p ? (size_t)img->width * img->height + p - 1 : 0);
            for (unsigned y = 0; y < (p ? img->height / 2 : img->height); ++y)
                for (unsigned x = 0; x < (p ? img->width / 2 : img->width); ++x)
                    ((uint8_t*)plane.data)[(size_t)y * plane.rowStride + x * plane.pixelStride] =
                        src[(size_t)y * img->width + x * (p ? 2 : 1)];
        }
    }
    AHardwareBuffer_unlock(img->ahb, nullptr);
    img->mapped = nullptr;
}

} // extern "C"

#endif // __ANDROID__
