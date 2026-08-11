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
// 1. RGBA8, not BGRA8. AHardwareBuffer simply has no BGRA format. A channel
//    here reports FL_FORMAT_RGBA8 from flQuery, and a portable producer writes
//    the byte order the channel says rather than assuming.
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
// Sync matches Linux: no shared fence. flSubmit means "I have finished
// writing" - true for a CPU producer (the auto-unlock flushes), and a GPU
// producer must have flushed. Slot recycling is monotonic (value V frees every
// submit <= V), the lesson the Linux backend learned from a bursty decoder.

#define FL_EXPORTS
#include "framelink.h"
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
    uint64_t strideBytes = 0;
};

struct flChannel {
    bool isConsumer = false;
    std::string name;
    flChannelInfo info{};
    uint32_t flags = 0;

    std::vector<flImage*> images;
    uint64_t readyValue = 0;

    // consumer
    fl::Channel peer;
    std::thread accept;
    std::atomic<bool> stop{false};
    std::atomic<bool> peerGone{false};
    std::mutex pendingMutex;
    bool hasPending = false;
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
    return f == FL_FORMAT_RGBA8 ? AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM : 0;
}

void freeImage(flImage* img) {
    if (img->mapped && img->ahb) AHardwareBuffer_unlock(img->ahb, nullptr);
    if (img->ahb) AHardwareBuffer_release(img->ahb);
    delete img;
}

// Allocate (or re-allocate) the ring.
bool reallocRing(flChannel* ch, uint32_t w, uint32_t h, uint32_t poolSize) {
    ++ch->info.generation; // a new ring - anyone who imported the old one must redo it
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
            return false;
        }
        AHardwareBuffer_Desc got{};
        AHardwareBuffer_describe(img->ahb, &got);
        img->strideBytes = (uint64_t)got.stride * 4; // stride is in PIXELS
        ch->images.push_back(img);
    }
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
    if (fr->readyValue > ch->consumedValue) ch->consumedValue = fr->readyValue;
}

// Producer: drain any queued control messages without blocking.
void drainProducerMessages(flChannel* ch) {
    for (;;) {
        pollfd pfd{ch->link.pollFd(), POLLIN, 0};
        if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) return;
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
                if (g && g->width && g->height && g->width <= 8192 && g->height <= 8192 &&
                    (g->width != ch->info.width || g->height != ch->info.height)) {
                    {
                        std::lock_guard<std::mutex> hold(ch->pendingMutex);
                        ch->hasPending = false; // stale: the old ring is going away
                    }
                    const uint32_t pool = (uint32_t)ch->images.size();
                    for (flImage* img : ch->images) freeImage(img);
                    ch->images.clear();
                    ch->info.width = g->width;
                    ch->info.height = g->height;
                    if (!reallocRing(ch, g->width, g->height, pool)) {
                        FL_LOG_ERR("framelink: reallocation to %ux%u failed", g->width,
                                   g->height);
                        break;
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
    return flCreateChannelEx(name, width, height, format, poolSize, flags, out);
}

FL_API flResult flCreateChannelEx(const char* name, uint32_t width, uint32_t height,
                                  flFormat format, uint32_t poolSize, uint32_t flags,
                                  flChannel** out) {
    if (!name || !out || !width || !height) return FL_INVALID;
    if (!ahbFormatFor(format)) return FL_FORMAT_UNSUPPORTED; // RGBA8 only - no BGRA AHB exists
    if (!poolSize || poolSize > fl::kMaxPool) return FL_INVALID;

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
        return FL_OOM;
    }

    ch->accept = std::thread(acceptLoop, ch);
    *out = ch;
    return FL_OK;
}

FL_API flResult flCreateChannel(const char* name, uint32_t width, uint32_t height,
                                flFormat format, uint32_t poolSize, flChannel** out) {
    return flCreateChannelEx(name, width, height, format, poolSize, 0, out);
}

FL_API flResult flOpenProducer(const char* name, flChannel** out) {
    if (!name || !out) return FL_INVALID;
    auto* ch = new flChannel();
    ch->name = name;
    ch->link = fl::Channel::connect(name, 300);
    if (!ch->link.connected()) {
        delete ch;
        return FL_NOT_FOUND;
    }

    auto helloMsg = ch->link.recv();
    if (helloMsg && helloMsg->type == fl::MsgType::Busy) {
        delete ch;
        return FL_BUSY;
    }
    const auto* srv = helloMsg && helloMsg->type == fl::MsgType::Hello
                          ? helloMsg->as<fl::HelloMsg>()
                          : nullptr;
    if (!srv) {
        delete ch;
        return FL_DISCONNECTED;
    }
    if (srv->version != FL_VERSION) {
        FL_LOG_ERR("framelink: channel '%s' speaks version %u, we speak %u", name,
                   srv->version, (unsigned)FL_VERSION);
        delete ch;
        return FL_VERSION_MISMATCH;
    }
    fl::HelloMsg mine{};
    mine.version = FL_VERSION;
    mine.pid = (uint32_t)getpid();
    if (!ch->link.send(fl::MsgType::Hello, mine)) {
        delete ch;
        return FL_INTERNAL;
    }

    auto ring = ch->link.recv();
    const auto* rd = ring && ring->type == fl::MsgType::RingDesc
                         ? ring->as<fl::RingDescMsg>()
                         : nullptr;
    if (!rd || rd->poolSize == 0 || rd->poolSize > fl::kMaxPool) {
        delete ch;
        return FL_DISCONNECTED;
    }
    ch->info.version = FL_VERSION;
    if (importRing(ch, rd) != FL_OK) {
        flClose(ch);
        return FL_DISCONNECTED;
    }
    *out = ch;
    return FL_OK;
}

FL_API flResult flRequestGeometry(flChannel* ch, uint32_t width, uint32_t height,
                                  flFormat format) {
    if (!ch || ch->isConsumer || !width || !height) return FL_INVALID;
    if (!ch->link.connected()) return FL_DISCONNECTED;
    fl::GeometryMsg g{};
    g.width = width;
    g.height = height;
    g.format = (uint32_t)format;
    if (!ch->link.send(fl::MsgType::RequestGeometry, g)) return FL_DISCONNECTED;
    // The consumer always answers with a RingDesc - the current ring if it
    // declined. We are the only reader (see the file header), so anything else
    // arriving first is a release to apply inline, never a message to lose.
    for (;;) {
        auto reply = ch->link.recv();
        if (!reply) return FL_DISCONNECTED;
        if (reply->type == fl::MsgType::FrameReleased) {
            applyRelease(ch, *reply);
            continue;
        }
        const auto* rd = reply->type == fl::MsgType::RingDesc ? reply->as<fl::RingDescMsg>()
                                                              : nullptr;
        if (!rd || rd->poolSize == 0 || rd->poolSize > fl::kMaxPool) return FL_DISCONNECTED;
        return importRing(ch, rd);
    }
}

FL_API flResult flAcquireBuffer(flChannel* ch, flBuffer* out, uint32_t timeoutMs) {
    if (!ch || ch->isConsumer || !out) return FL_INVALID;
    if (!ch->link.connected()) return FL_DISCONNECTED;
    for (uint32_t waited = 0;; waited += 2) {
        drainProducerMessages(ch);
        for (size_t i = 0; i < ch->images.size(); ++i) {
            if (ch->slotSubmitValue[i] <= ch->consumedValue) {
                // Pin until flEndSubmit records the real value (the sentinel is
                // unreachable by the consumer - it can only release submitted
                // values). Acquiring and never submitting leaks the slot.
                ch->slotSubmitValue[i] = UINT64_MAX;
                out->slot = (uint32_t)i;
                out->image = ch->images[i];
                return FL_OK;
            }
        }
        if (waited >= timeoutMs) return FL_TIMEOUT;
        usleep(2000);
    }
}

FL_API flResult flBeginSubmit(flChannel* ch, const flBuffer* buf, uint64_t* signalValue) {
    if (!ch || ch->isConsumer || !buf || !signalValue) return FL_INVALID;
    if (!ch->link.connected()) return FL_DISCONNECTED;
    *signalValue = ++ch->readyValue;
    return FL_OK;
}

FL_API flResult flEndSubmit(flChannel* ch, const flBuffer* buf, uint64_t signalValue,
                            int64_t ptsNs) {
    if (!ch || ch->isConsumer || !buf || buf->slot >= ch->images.size()) return FL_INVALID;
    if (!ch->link.connected()) return FL_DISCONNECTED;
    // The cache boundary: a mapped buffer's CPU writes are not GPU-visible
    // until unlocked. Automatic, so forgetting cannot produce the
    // works-on-one-device failure mode.
    flImageUnmap(buf->image);
    ch->slotSubmitValue[buf->slot] = signalValue;
    fl::FrameReadyMsg fr{};
    fr.slot = buf->slot;
    fr.readyValue = signalValue;
    fr.ptsNs = ptsNs;
    return ch->link.send(fl::MsgType::FrameReady, fr) ? FL_OK : FL_DISCONNECTED;
}

FL_API flResult flSubmit(flChannel* ch, const flBuffer* buf, int64_t ptsNs) {
    uint64_t v = 0;
    flResult r = flBeginSubmit(ch, buf, &v);
    return r == FL_OK ? flEndSubmit(ch, buf, v, ptsNs) : r;
}

FL_API flResult flSharedConsumedFence(flChannel*, void**) {
    return FL_FORMAT_UNSUPPORTED; // no shared fences on this backend yet
}

FL_API flResult flSharedReadyFence(flChannel*, void**) {
    return FL_FORMAT_UNSUPPORTED; // ditto; FL_GPU_SYNC is a no-op here
}

FL_API flResult flAcquireFrame(flChannel* ch, flFrame* out, uint32_t timeoutMs) {
    if (!ch || !ch->isConsumer || !out) return FL_INVALID;
    for (uint32_t waited = 0;; waited += 2) {
        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            if (ch->hasPending) {
                ch->hasPending = false;
                out->slot = ch->pendingSlot;
                out->image = ch->images[ch->pendingSlot];
                out->ptsNs = ch->pendingPts;
                out->sequence = ch->sequence;
                out->_readyValue = ch->pendingReady;
                return FL_OK;
            }
        }
        if (ch->peerGone.load()) return FL_DISCONNECTED;
        if (waited >= timeoutMs) return FL_TIMEOUT;
        usleep(2000);
    }
}

FL_API flResult flRelease(flChannel* ch, const flFrame* frame) {
    if (!ch || !ch->isConsumer || !frame || frame->slot >= ch->images.size()) return FL_INVALID;
    fl::FrameReadyMsg fr{};
    fr.slot = frame->slot;
    fr.readyValue = frame->_readyValue;
    std::lock_guard<std::mutex> hold(ch->pendingMutex);
    if (!ch->peer.connected()) return FL_OK;
    return ch->peer.send(fl::MsgType::FrameReleased, fr) ? FL_OK : FL_DISCONNECTED;
}

FL_API flResult flQuery(flChannel* ch, flChannelInfo* out) {
    if (!ch || !out) return FL_INVALID;
    *out = ch->info;
    return FL_OK;
}

FL_API void flClose(flChannel* ch) {
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
    if (!img || !pixels) return FL_INVALID;
    if (img->mapped) {
        *pixels = img->mapped;
        if (stride) *stride = img->strideBytes;
        return FL_OK;
    }
    void* p = nullptr;
    if (AHardwareBuffer_lock(img->ahb,
                             AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                                 AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,
                             -1, nullptr, &p) != 0 ||
        !p)
        return FL_FORMAT_UNSUPPORTED; // not allocated with FL_MAP_CPU
    img->mapped = p;
    *pixels = p;
    if (stride) *stride = img->strideBytes;
    return FL_OK;
}

FL_API void flImageUnmap(flImage* img) {
    if (!img || !img->mapped) return;
    AHardwareBuffer_unlock(img->ahb, nullptr);
    img->mapped = nullptr;
}

} // extern "C"

#endif // __ANDROID__
