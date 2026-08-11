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
// 1. Sync is weaker. Windows carries a shared timeline fence pair, so a
//    consumer can wait on the GPU work that produced a frame. Here, flSubmit
//    means "I have finished writing" and there is no fence to wait on: true for
//    a CPU producer and for a GPU producer that flushed, not enough for one
//    that did not. Explicit sync (DMA_BUF_IOCTL_EXPORT_SYNC_FILE, or a Vulkan
//    timeline semaphore fd) is the next piece of work. flSharedReadyFence
//    therefore returns FL_FORMAT_UNSUPPORTED rather than pretending.
//
// 2. Slot recycling is explicit. Windows lets the producer poll the shared
//    consumed fence; with no shared fence the consumer sends FrameReleased.

#define FL_EXPORTS
#include "framelink.h"
#include "framelink_dmabuf.h"

#ifndef _WIN32

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
    void* mapped = nullptr;
    void* mapCookie = nullptr; // gbm_bo_map cookie (consumer) or MAP_FAILED marker
    size_t mapLength = 0;      // producer: plain mmap of the dma-buf
};

struct flChannel {
    bool isConsumer = false;
    std::string name;
    flChannelInfo info{};
    uint32_t flags = 0;

    int drmFd = -1;
    gbm_device* gbm = nullptr;

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

    // producer
    fl::Channel link;
    std::mutex freeMutex;
    std::vector<bool> slotBusy;
    std::thread releaseRx;
};

namespace {

uint32_t fourccFor(flFormat f) {
    // ARGB8888 is little-endian BGRA in memory, matching FL_FORMAT_BGRA8.
    return f == FL_FORMAT_BGRA8 ? DRM_FORMAT_ARGB8888 : 0;
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
    if (img->mapCookie && img->bo) gbm_bo_unmap(img->bo, img->mapCookie);
    if (img->mapped && img->mapLength) munmap(img->mapped, img->mapLength);
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
        fds.push_back(ch->images[i]->desc.fd[0]);
    }
    return link.sendWithFds(fl::MsgType::RingDesc, &rd, sizeof(rd), fds.data(), fds.size());
}

// Allocate (or re-allocate) the ring. Shared by channel creation and by
// flRequestGeometry, which is the only reason it is not inline.
bool reallocRing(flChannel* ch, uint32_t w, uint32_t h, uint32_t poolSize) {
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
            return false;
        }
        memset(&img->desc, 0, sizeof(img->desc));
        img->desc.planes = 1;
        img->desc.fourcc = gbm_bo_get_format(img->bo);
        img->desc.modifier = gbm_bo_get_modifier(img->bo);
        img->desc.fd[0] = gbm_bo_get_fd(img->bo);
        img->desc.stride[0] = gbm_bo_get_stride(img->bo);
        img->desc.offset[0] = gbm_bo_get_offset(img->bo, 0);
        if (img->desc.fd[0] < 0) {
            freeImage(img);
            return false;
        }
        ch->images.push_back(img);
    }
    return true;
}

void acceptLoop(flChannel* ch) {
    while (!ch->stop.load()) {
        fl::Channel link = fl::Channel::listen(ch->name);
        if (!link.connected() || ch->stop.load()) return;

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
            auto msg = ch->peer.recv();
            if (!msg || msg->type == fl::MsgType::Bye) break;
            if (msg->type == fl::MsgType::RequestGeometry) {
                const auto* g = msg->as<fl::GeometryMsg>();
                if (g && g->width && g->height && g->width <= 8192 && g->height <= 8192 &&
                    (g->width != ch->info.width || g->height != ch->info.height)) {
                    std::lock_guard<std::mutex> hold(ch->pendingMutex);
                    ch->hasPending = false; // stale: the old ring is going away
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
            // Latest-frame-wins, as on Windows: a slow consumer must not stall
            // the producer.
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

// Producer: the consumer tells us which slots are free again.
void releaseLoop(flChannel* ch) {
    for (;;) {
        auto msg = ch->link.recv();
        if (!msg) break;
        if (msg->type != fl::MsgType::FrameReleased) continue;
        const auto* fr = msg->as<fl::FrameReadyMsg>();
        if (!fr || fr->slot >= ch->slotBusy.size()) continue;
        std::lock_guard<std::mutex> hold(ch->freeMutex);
        ch->slotBusy[fr->slot] = false;
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

FL_API flResult flCreateChannelEx(const char* name, uint32_t width, uint32_t height,
                                  flFormat format, uint32_t poolSize, uint32_t flags,
                                  flChannel** out) {
    if (!name || !out || !width || !height) return FL_INVALID;
    if (!fourccFor(format)) return FL_FORMAT_UNSUPPORTED;
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

    ch->gbm = openRenderNode(&ch->drmFd);
    if (!ch->gbm) {
        delete ch;
        FL_LOG_ERR("framelink: no usable /dev/dri render node");
        return FL_INTERNAL;
    }

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
    if (!rd || rd->poolSize == 0 || rd->poolSize > fl::kMaxPool ||
        ring->fds.size() != rd->poolSize) {
        delete ch;
        return FL_DISCONNECTED;
    }
    for (uint32_t i = 0; i < rd->poolSize; ++i) {
        auto* img = new flImage();
        img->desc.planes = 1;
        img->desc.fourcc = rd->fourcc;
        img->desc.modifier = rd->modifier;
        img->desc.fd[0] = ring->fds[i]; // ours now; closed in flClose
        img->desc.stride[0] = rd->stride[i];
        img->desc.offset[0] = rd->offset[i];
        ch->images.push_back(img);
    }
    ch->slotBusy.assign(rd->poolSize, false);
    ch->info.version = FL_VERSION;
    ch->info.width = rd->width;
    ch->info.height = rd->height;
    ch->info.format = (flFormat)rd->format;
    ch->info.poolSize = rd->poolSize;
    ch->releaseRx = std::thread(releaseLoop, ch);
    *out = ch;
    return FL_OK;
}

// Producer: adopt a ring description. Used at attach and after a realloc.
static flResult importRing(flChannel* ch, const fl::RingDescMsg* rd,
                           const std::vector<int>& fds) {
    if (fds.size() != rd->poolSize) return FL_DISCONNECTED;
    for (flImage* img : ch->images) freeImage(img);
    ch->images.clear();
    for (uint32_t i = 0; i < rd->poolSize; ++i) {
        auto* img = new flImage();
        img->desc.planes = 1;
        img->desc.fourcc = rd->fourcc;
        img->desc.modifier = rd->modifier;
        img->desc.fd[0] = fds[i];
        img->desc.stride[0] = rd->stride[i];
        img->desc.offset[0] = rd->offset[i];
        ch->images.push_back(img);
    }
    ch->slotBusy.assign(rd->poolSize, false);
    ch->info.width = rd->width;
    ch->info.height = rd->height;
    ch->info.format = (flFormat)rd->format;
    ch->info.poolSize = rd->poolSize;
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
    // The consumer always answers with a RingDesc - the current one if it
    // declined - so our view is never left stale.
    auto reply = ch->link.recv();
    const auto* rd = reply && reply->type == fl::MsgType::RingDesc
                         ? reply->as<fl::RingDescMsg>()
                         : nullptr;
    if (!rd) return FL_DISCONNECTED;
    ch->readyValue = 0;
    return importRing(ch, rd, reply->fds);
}

FL_API flResult flAcquireBuffer(flChannel* ch, flBuffer* out, uint32_t timeoutMs) {
    if (!ch || ch->isConsumer || !out) return FL_INVALID;
    if (!ch->link.connected()) return FL_DISCONNECTED;
    for (uint32_t waited = 0;; waited += 2) {
        {
            std::lock_guard<std::mutex> hold(ch->freeMutex);
            for (size_t i = 0; i < ch->images.size(); ++i) {
                if (!ch->slotBusy[i]) {
                    ch->slotBusy[i] = true;
                    out->slot = (uint32_t)i;
                    out->image = ch->images[i];
                    return FL_OK;
                }
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

FL_API flResult flSharedReadyFence(flChannel*, void**) {
    // Honest refusal: there is no shared fence on this backend yet, and handing
    // back something unusable would be worse than saying so.
    return FL_FORMAT_UNSUPPORTED;
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

FL_API flResult flReconfigure(flChannel*, uint32_t, uint32_t, flFormat) {
    return FL_INTERNAL; // not implemented on this backend yet
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
        if (ch->releaseRx.joinable()) ch->releaseRx.detach();
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

FL_API flResult flImageMap(flImage* img, void** pixels, uint64_t* stride) {
    if (!img || !pixels) return FL_INVALID;
    if (img->mapped) {
        *pixels = img->mapped;
        if (stride) *stride = img->desc.stride[0];
        return FL_OK;
    }
    // Map the dma-buf directly. This works for LINEAR buffers on both sides,
    // which is why FL_MAP_CPU forces GBM_BO_USE_LINEAR at allocation; a tiled
    // buffer would map to bytes in an order nobody can use.
    // The dma-buf's own size is authoritative - GBM may have padded height or
    // stride, so width*stride would under-map and tear the last rows.
    struct stat st;
    if (fstat(img->desc.fd[0], &st) != 0) return FL_INTERNAL;
    void* p = mmap(nullptr, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   img->desc.fd[0], 0);
    if (p == MAP_FAILED) return FL_FORMAT_UNSUPPORTED;
    img->mapped = p;
    img->mapLength = (size_t)st.st_size;
    *pixels = p;
    if (stride) *stride = img->desc.stride[0];
    return FL_OK;
}

FL_API void flImageUnmap(flImage* img) {
    if (!img || !img->mapped) return;
    munmap(img->mapped, img->mapLength);
    img->mapped = nullptr;
    img->mapLength = 0;
}

} // extern "C"

#endif // !_WIN32
