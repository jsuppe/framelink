#include "framelink_trace.hpp"
// framelink Linux backend. See framelink.h.
//
// The consumer allocates the ring with GBM on a render node and exports each
// buffer as a dma-buf fd; the fds reach the producer as SCM_RIGHTS on the
// control socket. Nothing is copied afterwards - the buffer the producer draws
// into is the exact allocation the consumer reads, the same guarantee the
// Windows backend makes with shared NT handles.
//
// TWO DIFFERENCES FROM WINDOWS, both deliberate and both visible to callers:
//
// 1. Native completion uses sync-file FDs, not Windows timeline handles.
//    flSubmit means writes are complete; flSubmitSyncFd can hand off queued
//    GPU work. FL_GPU_SYNC consumers import the ready FD themselves; default
//    consumers poll it within their acquisition timeout. Deferred release
//    keeps storage pinned until GPU reads complete.
//
// 2. Slot recycling is explicit. Windows lets the producer poll the shared
//    consumed fence; with no shared fence the consumer sends FrameReleased.

#define FL_EXPORTS
#include "framelink.h"
#include "framelink_sync_fd.h"
#include "fl_sync_fd.hpp"
#include "framelink_dmabuf.h"

#if !defined(_WIN32) && !defined(__ANDROID__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include "fl_layout.h"
#include <unistd.h>

#include <drm_fourcc.h>
#include <gbm.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fl_ipc.h"
#include "fl_log.h"
#include "fl_protocol.h"

struct flImage {
    gbm_bo* bo = nullptr; // consumer only
    flDmabuf desc{};
    uint32_t width=0,height=0;
    void* planeMaps[4]{};
    size_t planeLengths[4]{};
    flMappedImage planeView{};
};

struct flChannel {
    bool isConsumer = false;
    std::string name;
    flChannelInfo info{};
    uint32_t flags = 0;

    int drmFd = -1;
    gbm_device* gbm = nullptr;

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

    // producer. Slot recycling is MONOTONIC, exactly like the Windows
    // consumed fence: a FrameReleased carrying value V frees every slot whose
    // last submit was <= V. Per-slot booleans were the first design and they
    // LEAK under latest-wins: a consumer only ever holds the newest announced
    // frame, so a skipped frame's slot would be released by nobody - four
    // skips and the ring is dead, which a bursty decoder start produces in
    // well under a second (observed: an RTSP camera at 100% ring-drop while
    // its slower sibling ran clean).
    //
    // SINGLE-READER: all producer-side receives happen on the caller's thread.
    // The first design ran a background thread for FrameReleased, and that
    // thread RACED flRequestGeometry's recv for the RingDesc reply - whichever
    // recvmsg the kernel picked got the message, and thread startup timing was
    // the only reason the tests passed. flAcquireBuffer drains releases
    // non-blocking instead, and flRequestGeometry applies them inline while
    // waiting for its reply. One channel, one calling thread, by contract.
    fl::Channel link;
    std::vector<uint64_t> slotSubmitValue; // last submit counter per slot
    uint64_t consumedValue = 0;            // everything <= this is free
};

namespace {

uint32_t fourccFor(flFormat f) {
    // ARGB8888 is little-endian BGRA in memory, matching FL_FORMAT_BGRA8.
    return f == FL_FORMAT_BGRA8 ? DRM_FORMAT_ARGB8888 : f == FL_FORMAT_NV12 ? DRM_FORMAT_NV12 : f == FL_FORMAT_RGBA8 ? DRM_FORMAT_ABGR8888 : f == FL_FORMAT_RGB10A2 ? DRM_FORMAT_ABGR2101010 : 0;
}

gbm_device* openRenderNode(int* fdOut) {
    // Render nodes only: no DRM master, no seat, works headless and inside
    // containers - which is what a library wants.
    for (int i = 128; i < 136; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
        const int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        if (gbm_device* g = gbm_create_device(fd)) {
            *fdOut = fd;
            return g;
        }
        ::close(fd);
    }
    return nullptr;
}

void freeImage(flImage* img) {
    flImageUnmap(img);
    for (uint32_t i = 0; i < img->desc.planes; ++i)
        if (img->desc.fd[i] >= 0) ::close(img->desc.fd[i]);
    if (img->bo) gbm_bo_destroy(img->bo);
    delete img;
}

// Hand the current ring to a producer: fds as ancillary data, layout in the
// payload. Repeated after flRequestGeometry reallocates, which is why it is a
// function.
bool handOverRing(flChannel* ch, fl::Channel& link) {
    fl::RingDescMsg rd{};
    rd.generation = ch->info.generation;
    rd.width = ch->info.width;
    rd.height = ch->info.height;
    rd.format = (uint32_t)ch->info.format;
    rd.poolSize = (uint32_t)ch->images.size();
    rd.fourcc = ch->images[0]->desc.fourcc;
    rd.modifier = ch->images[0]->desc.modifier;
    std::vector<int> fds;
    for (size_t i = 0; i < ch->images.size(); ++i) {
        rd.stride[i] = ch->images[i]->desc.stride[0];
        rd.offset[i] = ch->images[i]->desc.offset[0];
        rd.planeCount[i]=ch->images[i]->desc.planes;
        for(uint32_t p=0;p<rd.planeCount[i];p++) {
            rd.planeStride[i][p]=ch->images[i]->desc.stride[p];
            rd.planeOffset[i][p]=ch->images[i]->desc.offset[p];
            fds.push_back(ch->images[i]->desc.fd[p]);
        }
    }
    return link.sendWithFds(fl::MsgType::RingDesc, &rd, sizeof(rd), fds.data(), fds.size());
}

// Allocate (or re-allocate) the ring. Shared by channel creation and by
// flRequestGeometry, which is the only reason it is not inline.
bool reallocRing(flChannel* ch, uint32_t w, uint32_t h, uint32_t poolSize) {
    std::vector<flImage*> images;
    auto fail=[&](){for(auto* image:images)freeImage(image);return false;};
    // Asking for RENDERING|LINEAR together is the natural thing to want and it
    // FAILS on NVIDIA's GBM backend - measured, each flag alone succeeds. Try
    // the pair, fall back to LINEAR alone.
    const uint32_t preferred = (ch->flags & FL_MAP_CPU)
                                   ? (GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR)
                                   : GBM_BO_USE_RENDERING;
    const uint32_t fallback =
        (ch->flags & FL_MAP_CPU) ? GBM_BO_USE_LINEAR : GBM_BO_USE_RENDERING;
    const uint32_t fourcc = fourccFor(ch->info.format);
    for (uint32_t i = 0; i < poolSize; ++i) {
        auto* img = new flImage();
        img->bo = gbm_bo_create(ch->gbm, w, h, fourcc, preferred);
        if (!img->bo && fallback != preferred) {
            img->bo = gbm_bo_create(ch->gbm, w, h, fourcc, fallback);
            if (img->bo && i == 0)
                FL_LOG("framelink: RENDERING|LINEAR refused, using LINEAR only "
                       "(CPU-mappable but may not be GPU-renderable)");
        }
        if (!img->bo) {
            delete img;
            return fail();
        }
        for(auto& fd:img->desc.fd) fd=-1;
        img->width=w; img->height=h;
        img->desc.planes=gbm_bo_get_plane_count(img->bo);
        img->desc.fourcc=gbm_bo_get_format(img->bo);
        img->desc.modifier=gbm_bo_get_modifier(img->bo);
        const uint32_t expected=ch->info.format==FL_FORMAT_NV12?2:1;
        if(img->desc.planes!=expected || ((ch->flags&FL_MAP_CPU) && img->desc.modifier!=DRM_FORMAT_MOD_LINEAR)) {
            img->desc.planes=expected;freeImage(img);return fail();
        }
        for(uint32_t p=0;p<img->desc.planes;p++) {
            img->desc.fd[p]=gbm_bo_get_fd_for_plane(img->bo,p);
            img->desc.stride[p]=gbm_bo_get_stride_for_plane(img->bo,p);
            img->desc.offset[p]=gbm_bo_get_offset(img->bo,p);
            if(img->desc.fd[p]<0){freeImage(img);return fail();}
        }
        images.push_back(img);
    }
    for(auto* image:ch->images)freeImage(image);
    ch->images=std::move(images);
    ch->info.width=w;ch->info.height=h;++ch->info.generation;
    ch->heldReady=ch->retiredValue=0;ch->sync.clear();
    return true;
}

void acceptLoop(flChannel* ch) {
    // ONE listener for the channel's whole life. The old per-producer
    // Channel::listen() released the socket name while a producer was
    // attached, so a second producer's connect() got ECONNREFUSED - "taken"
    // was indistinguishable from "no such channel", and slot walking needs
    // exactly that distinction (FL_BUSY vs FL_NOT_FOUND).
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

        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            ch->sync.advance(ch->retiredValue);
            if(ch->frameHeld || ch->retiredValue<ch->heldReady || !fl::fenceReady(ch->sync.pending)) {
                link.send(fl::MsgType::Busy);
                continue;
            }
            // A new producer starts its timeline at one. Old release values
            // must never authorize reuse on the replacement timeline.
            ch->hasPending=false;ch->sync.clear();
            ch->heldReady=ch->retiredValue=0;
            ++ch->info.generation;
            if (!handOverRing(ch, link)) continue;
        }

        ch->peerGone = false;
        FL_LOG("framelink: producer pid %u attached to '%s'", link.peerPid(), ch->name.c_str());
        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            ch->peer = std::move(link);
        }

        for (;;) {
            // Wait on the producer AND the listener at once: a second
            // producer must be told Busy NOW, not whenever the current one
            // next sends a frame - an idle producer would leave the walker
            // blocked indefinitely otherwise.
            pollfd fds[2] = {{ch->peer.pollFd(), POLLIN, 0}, {lst.pollFd(), POLLIN, 0}};
            if (poll(fds, 2, -1) < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ch->stop.load()) return;
            if (fds[1].revents & POLLIN) {
                fl::Channel extra = lst.accept();
                // The refusal is explicit, so flOpenProducer can answer
                // FL_BUSY instead of misreading the closed connection.
                if (extra.connected()) extra.send(fl::MsgType::Busy);
            }
            if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            auto msg = ch->peer.recv();
            if (!msg || msg->type == fl::MsgType::Bye) break;
            if (msg->type == fl::MsgType::RequestGeometry) {
                const auto* g = msg->as<fl::GeometryMsg>();
                if (g && g->format==(uint32_t)ch->info.format && fl::validGeometry(g->width,g->height,ch->info.format) &&
                    (g->width != ch->info.width || g->height != ch->info.height)) {
                    std::lock_guard<std::mutex> hold(ch->pendingMutex);
                    ch->sync.advance(ch->retiredValue);
                    if(ch->frameHeld || ch->retiredValue < ch->heldReady){if(!handOverRing(ch,ch->peer))break;continue;}
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
            // Latest-frame-wins, as on Windows: a slow consumer must not stall
            // the producer.
            if(fltrace::enabled()) {
                fltrace::Event e{"ipc.frame_received",fltrace::clockNs(),0,fltrace::id(ch->name.c_str()),
                    ch->sequence+1,fr->readyValue,fr->slot,fr->ptsNs,ch->hasPending?1:0};
                fltrace::record(e); // result=1: unconsumed pending frame replaced
            }
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

// Producer: apply one already-received release message.
void applyRelease(flChannel* ch, const fl::Message& msg) {
    if (msg.type != fl::MsgType::FrameReleased) return;
    const auto* fr = msg.as<fl::FrameReadyMsg>();
    if (!fr) return;
    // Monotonic: never backwards, and releasing value V frees the skipped
    // frames before V too, which per-slot release could not.
    ch->sync.releases.push_back({fr->readyValue,msg.fds.empty()?-1:msg.fds[0]});
    for(size_t i=1;i<msg.fds.size();i++)close(msg.fds[i]);
    ch->sync.advance(ch->consumedValue);
}

// Producer: drain queued control messages without blocking.
void drainProducerMessages(flChannel* ch) {
    for (;;) {
        pollfd pfd{ch->link.pollFd(), POLLIN, 0};
        if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & (POLLIN | POLLHUP | POLLERR))) return;
        auto msg = ch->link.recv();
        if (!msg) return;
        applyRelease(ch, *msg);
    }
}

} // namespace

extern "C" {
static flResult importRing(flChannel*,const fl::RingDescMsg*,const std::vector<int>&);

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

// The LUID is a Windows concept. Here the ring comes from a DRM render node,
// and which node was opened IS the adapter choice, so there is nothing to
// select with a LUID and ignoring it is the honest behaviour rather than a
// stub - a caller passing one gets the same channel it would have got anyway.
FL_API flResult flCreateChannelOnAdapter(const char* name, uint32_t width, uint32_t height,
                                         flFormat format, uint32_t poolSize, uint32_t flags,
                                         const uint8_t*, flChannel** out) {
    fltrace::Scope trace("flCreateChannelOnAdapter",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=(uint64_t)format;trace.event.slot=poolSize;trace.event.pts=flags;}

    return trace.result(flCreateChannelEx(name, width, height, format, poolSize, flags, out));
}

FL_API flResult flCreateChannelEx(const char* name, uint32_t width, uint32_t height,
                                  flFormat format, uint32_t poolSize, uint32_t flags,
                                  flChannel** out) {
    fltrace::Scope trace("flCreateChannelEx",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=(uint64_t)format;trace.event.slot=poolSize;trace.event.pts=flags;}

    if (!name || !out || !fl::validGeometry(width,height,format)) return trace.result(FL_INVALID);
    *out=nullptr;
    if (!fourccFor(format)) return trace.result(FL_FORMAT_UNSUPPORTED);
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

    ch->gbm = openRenderNode(&ch->drmFd);
    if (!ch->gbm) {
        delete ch;
        FL_LOG_ERR("framelink: no usable /dev/dri render node");
        return trace.result(FL_INTERNAL);
    }

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
        // The channel exists; someone else has it. Tell the walker so, rather
        // than letting a closed connection read as "no such channel".
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
    if(ring && ring->type==fl::MsgType::Busy){delete ch;return trace.result(FL_BUSY);}
    const auto* rd = ring && ring->type == fl::MsgType::RingDesc
                         ? ring->as<fl::RingDescMsg>()
                         : nullptr;
    if (!rd) { delete ch; return trace.result(FL_DISCONNECTED); }
    flResult imported=importRing(ch,rd,ring->fds);
    if(imported!=FL_OK){for(int fd:ring->fds)::close(fd);delete ch;return trace.result(imported);}
    ch->info.version=FL_VERSION;
    *out = ch;
    return trace.result(FL_OK);
}

// Producer: adopt a ring description. Used at attach and after a realloc.
static flResult importRing(flChannel* ch, const fl::RingDescMsg* rd,
                           const std::vector<int>& fds) {
    if(!rd || !rd->poolSize || rd->poolSize>fl::kMaxPool ||
       !fl::validGeometry(rd->width,rd->height,(flFormat)rd->format) ||
       rd->fourcc!=fourccFor((flFormat)rd->format) || !rd->fourcc) return FL_DISCONNECTED;
    uint32_t count=0,expected=rd->format==FL_FORMAT_NV12?2:1;
    for(uint32_t i=0;i<rd->poolSize;i++) {
        if(rd->planeCount[i]!=expected)return FL_DISCONNECTED;
        count+=expected;
        for(uint32_t p=0;p<expected;p++) {
            uint64_t bytes=expected==2?rd->width:(uint64_t)rd->width*4;
            if(rd->planeStride[i][p]<bytes)return FL_DISCONNECTED;
        }
    }
    if(fds.size()!=count)return FL_DISCONNECTED;
    for (flImage* img : ch->images) freeImage(img);
    ch->images.clear();
    uint32_t n=0;
    for(uint32_t i=0;i<rd->poolSize;i++) {
        auto* img=new flImage();
        img->width=rd->width;img->height=rd->height;
        img->desc.planes=expected;img->desc.fourcc=rd->fourcc;img->desc.modifier=rd->modifier;
        for(uint32_t p=0;p<expected;p++) {
            img->desc.fd[p]=fds[n++];img->desc.stride[p]=rd->planeStride[i][p];img->desc.offset[p]=rd->planeOffset[i][p];
        }
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

FL_API flResult flRequestGeometry(flChannel* ch, uint32_t width, uint32_t height,
                                  flFormat format) {
    fltrace::Scope trace("flRequestGeometry", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !fl::validGeometry(width,height,format)) return trace.result(FL_INVALID);
    if(!fourccFor(format))return trace.result(FL_FORMAT_UNSUPPORTED);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    fl::GeometryMsg g{};
    g.width = width;
    g.height = height;
    g.format = (uint32_t)format;
    if (!ch->link.send(fl::MsgType::RequestGeometry, g)) return trace.result(FL_DISCONNECTED);
    // The consumer always answers with a RingDesc - the current one if it
    // declined - so our view is never left stale. We are the only reader
    // (single-reader producer; see the struct comment), so anything arriving
    // first is a release to apply inline, never a message to lose.
    for (;;) {
        auto reply = ch->link.recv();
        if (!reply) return trace.result(FL_DISCONNECTED);
        if (reply->type == fl::MsgType::FrameReleased) {
            applyRelease(ch, *reply);
            continue;
        }
        const auto* rd = reply->type == fl::MsgType::RingDesc ? reply->as<fl::RingDescMsg>()
                                                              : nullptr;
        if (!rd) return trace.result(FL_DISCONNECTED);
        // A refused/no-op resize must preserve outstanding slot ownership.
        if (rd->generation == ch->info.generation) {
            for (int fd : reply->fds) ::close(fd);
            return trace.result(FL_OK);
        }
        ch->sync.clear();
        ch->readyValue = 0;
        flResult result=importRing(ch, rd, reply->fds);
        if(result!=FL_OK)for(int fd:reply->fds)::close(fd);
        return trace.result(result);
    }
}

FL_API flResult flAcquireBuffer(flChannel* ch, flBuffer* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireBuffer", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !out) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    for (uint32_t waited = 0;; waited += 2) {
        drainProducerMessages(ch);
        if(!ch->link.connected())return trace.result(FL_DISCONNECTED);
        ch->sync.advance(ch->consumedValue);
        for (size_t i = 0; i < ch->images.size(); ++i) {
            // Free once the consumer has promised never to read this slot's
            // previous contents again - same rule as Windows.
            if (ch->slotSubmitValue[i] <= ch->consumedValue) {
                // Pin against re-acquisition until flEndSubmit records the
                // real value. The consumer can never release up to a value
                // that was not submitted, so the sentinel is unreachable.
                // (Acquiring and never submitting leaks the slot until the
                // ring is reallocated - do not do that.)
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
    fltrace::Scope trace("flBeginSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !buf || !signalValue) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    *signalValue = ++ch->readyValue;
    if(trace.active) trace.event.fence=*signalValue;
    return trace.result(FL_OK);
}

FL_API flResult flEndSubmit(flChannel* ch, const flBuffer* buf, uint64_t signalValue,
                            int64_t ptsNs) {
    fltrace::Scope trace("flEndSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));
    if(trace.active) { trace.event.pts=ptsNs; trace.event.slot=buf?buf->slot:0; }
    if(trace.active) trace.event.fence=signalValue;

    if (!ch || ch->isConsumer || !buf || buf->slot >= ch->images.size()) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    ch->slotSubmitValue[buf->slot] = signalValue;
    fl::FrameReadyMsg fr{};
    fr.slot = buf->slot;
    fr.readyValue = signalValue;
    fr.ptsNs = ptsNs;
    return trace.result(ch->link.send(fl::MsgType::FrameReady, fr) ? FL_OK : FL_DISCONNECTED);
}

FL_API flResult flSubmit(flChannel* ch, const flBuffer* buf, int64_t ptsNs) {
    fltrace::Scope trace("flSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));
    if(trace.active) { trace.event.pts=ptsNs; trace.event.slot=buf?buf->slot:0; }

    uint64_t v = 0;
    flResult r = flBeginSubmit(ch, buf, &v);
    return trace.result(r == FL_OK ? flEndSubmit(ch, buf, v, ptsNs) : r);
}

FL_API flResult flSharedConsumedFence(flChannel*, void**) {
    return FL_FORMAT_UNSUPPORTED; // use framelink_sync_fd.h for native completion FDs
}

FL_API flResult flSharedReadyFence(flChannel*, void**) {
    // Native sync-file FDs are exposed by framelink_sync_fd.h instead.
    return FL_FORMAT_UNSUPPORTED;
}

FL_API flResult flAcquireFrame(flChannel* ch, flFrame* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireFrame", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || !ch->isConsumer || !out) return trace.result(FL_INVALID);
    for (uint32_t waited = 0;; waited += 2) {
        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            if (ch->hasPending && !ch->frameHeld &&
                ((ch->flags & FL_GPU_SYNC) || fl::fenceReady(ch->sync.pending))) {
                ch->sync.held=ch->sync.pending;ch->sync.pending=-1;
                ch->frameHeld=true;
                ch->heldReady=ch->pendingReady;ch->heldSlot=ch->pendingSlot;
                ch->hasPending = false;
                out->slot = ch->pendingSlot;
                out->image = ch->images[ch->pendingSlot];
                out->ptsNs = ch->pendingPts;
                out->sequence = ch->sequence;
                out->_readyValue = ch->pendingReady;
                if(trace.active){trace.event.slot=out->slot;trace.event.sequence=out->sequence;trace.event.pts=out->ptsNs;trace.event.fence=out->_readyValue;}
                return trace.result(FL_OK);
            }
        }
        if (ch->peerGone.load()) return trace.result(FL_DISCONNECTED);
        if (waited >= timeoutMs) return trace.result(FL_TIMEOUT);
        usleep(2000);
    }
}

FL_API flResult flReleaseSyncFd(flChannel* ch, const flFrame* frame, int consumedFd) {
    fltrace::Scope trace("flRelease", fltrace::id(ch ? ch->name.c_str() : nullptr));
    if(trace.active && frame) {trace.event.sequence=frame->sequence;trace.event.pts=frame->ptsNs;trace.event.fence=frame->_readyValue;trace.event.slot=frame->slot;}

    if (!ch || !ch->isConsumer || !frame || frame->slot >= ch->images.size()) return trace.result(FL_INVALID);
    fl::FrameReadyMsg fr{};
    fr.slot = frame->slot;
    fr.readyValue = frame->_readyValue;
    std::lock_guard<std::mutex> hold(ch->pendingMutex);
    if(!ch->frameHeld || frame->_readyValue!=ch->heldReady || frame->slot!=ch->heldSlot)return trace.result(FL_INVALID);
    int localFd=consumedFd>=0?dup(consumedFd):-1;
    if(consumedFd>=0 && localFd<0)return trace.result(FL_INTERNAL);
    ch->sync.releases.push_back({frame->_readyValue,localFd});
    ch->sync.advance(ch->retiredValue);
    ch->frameHeld=false;
    if(ch->sync.held>=0)close(ch->sync.held);ch->sync.held=-1;
    if (!ch->peer.connected()) return trace.result(FL_OK);
    return trace.result(ch->peer.sendWithFds(fl::MsgType::FrameReleased, &fr, sizeof(fr), consumedFd>=0?&consumedFd:nullptr, consumedFd>=0?1:0) ? FL_OK : FL_DISCONNECTED);
}

FL_API flResult flReconfigure(flChannel*, uint32_t, uint32_t, flFormat) {
    return FL_INTERNAL; // not implemented on this backend yet
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
    std::lock_guard<std::mutex> hold(ch->pendingMutex);
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
    if (ch->gbm) gbm_device_destroy(ch->gbm);
    if (ch->drmFd >= 0) ::close(ch->drmFd);
    delete ch;
}

// ---- dma-buf interop ---------------------------------------------------------

FL_API flResult flImageDmabuf(flImage* img, flDmabuf* out) {
    if (!img || !out) return FL_INVALID;
    *out = img->desc;
    return FL_OK;
}

FL_API flResult flImageMapPlanes(flImage* img,flMappedImage* out) {
    fltrace::Scope trace("flImageMapPlanes", (uint64_t)(uintptr_t)img);

    if(!img || !out)return trace.result(FL_INVALID);
    *out={};
    if(img->desc.modifier!=DRM_FORMAT_MOD_LINEAR)return trace.result(FL_FORMAT_UNSUPPORTED);
    if(img->planeView.planes){*out=img->planeView;return trace.result(FL_OK);}
    uint32_t planes=img->desc.fourcc==DRM_FORMAT_NV12?2:1;
    for(uint32_t p=0;p<planes;p++) {
        uint32_t rows=p?img->height/2:img->height;
        uint32_t bytes=planes==2?img->width:img->width*4;
        struct stat st{};
        if(fstat(img->desc.fd[p],&st)!=0 || st.st_size<=0 ||
           !fl::planeFits(st.st_size,img->desc.offset[p],img->desc.stride[p],rows,bytes)) {
            flImageUnmap(img);return trace.result(FL_INVALID);
        }
        void* base=mmap(nullptr,st.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,img->desc.fd[p],0);
        if(base==MAP_FAILED){flImageUnmap(img);return trace.result(FL_FORMAT_UNSUPPORTED);}
        dma_buf_sync sync{DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW};
        int result;do{result=ioctl(img->desc.fd[p],DMA_BUF_IOCTL_SYNC,&sync);}while(result<0 && errno==EINTR);
        if(result<0){munmap(base,st.st_size);flImageUnmap(img);return trace.result(FL_INTERNAL);}
        img->planeMaps[p]=base;img->planeLengths[p]=st.st_size;
        img->planeView.plane[p]={(char*)base+img->desc.offset[p],img->desc.stride[p],bytes,rows};
    }
    img->planeView.planes=planes;*out=img->planeView;return trace.result(FL_OK);
}
FL_API flResult flImageMap(flImage* img,void** pixels,uint64_t* stride) {
    fltrace::Scope trace("flImageMap", (uint64_t)(uintptr_t)img);

    if(!img || !pixels)return trace.result(FL_INVALID);
    *pixels=nullptr;
    if(img->desc.fourcc==DRM_FORMAT_NV12)return trace.result(FL_FORMAT_UNSUPPORTED);
    flMappedImage view{};flResult r=flImageMapPlanes(img,&view);
    if(r==FL_OK){*pixels=view.plane[0].pixels;if(stride)*stride=view.plane[0].stride;}
    return trace.result(r);
}
FL_API void flImageUnmap(flImage* img) {
    fltrace::Scope trace("flImageUnmap",(uint64_t)(uintptr_t)img);
    if(!img)return;
    for(uint32_t p=0;p<4;p++)if(img->planeMaps[p]) {
        dma_buf_sync sync{DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW};
        int result;do{result=ioctl(img->desc.fd[p],DMA_BUF_IOCTL_SYNC,&sync);}while(result<0 && errno==EINTR);
        munmap(img->planeMaps[p],img->planeLengths[p]);img->planeMaps[p]=nullptr;img->planeLengths[p]=0;
    }
    img->planeView={};
}

} // extern "C"

#endif // !_WIN32
