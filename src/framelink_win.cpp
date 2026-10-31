#include "framelink_trace.hpp"
// framelink Windows backend. See framelink.h.
//
// Shared NT-handle BGRA/NV12 textures plus a shared ready/consumed fence pair. The
// consumer allocates and duplicates them into the producer; nothing is copied
// afterwards - the texture the producer draws into is the exact allocation the
// consumer reads.
//
// The control channel and wire format are framelink's own (src/fl_ipc.h,
// src/fl_protocol.h). The spike borrowed vkrender's, which was right for
// testing the API shape against a proven transport, but a library cannot ship
// depending on an application's protocol.

#define FL_EXPORTS
#include "framelink.h"
#include "fl_layout.h"
#include "framelink_d3d11.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <future>

#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fl_ipc.h"
#include "fl_log.h"
#include "fl_protocol.h"

using Microsoft::WRL::ComPtr;

struct flImage {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<ID3D11DeviceContext> mapContext;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    ~flImage() { if(mapped.pData && mapContext && staging) mapContext->Unmap(staging.Get(),0); }
    bool producer = false;
    HANDLE shared = nullptr; // borrowed by callers; owned here
};

struct flChannel {
    bool isConsumer = false;
    uint32_t flags = 0;
    std::string name;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext4> ctx;
    ComPtr<ID3D11Fence> readyFence, consumedFence;
    HANDLE readyShared = nullptr, consumedShared = nullptr;
    HANDLE waitEvent = nullptr;

    std::vector<flImage*> images;
    std::vector<uint64_t> slotReadyValue; // producer: last value signalled per slot
    uint64_t readyValue = 0;              // producer: monotonic submit counter
    uint64_t consumedValue = 0;           // consumer: monotonic release counter

    flChannelInfo info{};

    // --- consumer only ---
    fl::Channel peer;
    std::thread accept;
    std::promise<bool> listenerReady;
    std::atomic<bool> stop{false};
    std::atomic<bool> peerGone{false};
    std::mutex pendingMutex;
    bool hasPending = false;
    bool frameHeld = false;
    uint64_t heldReady = 0;
    uint32_t heldSlot = 0;
    uint32_t pendingSlot = 0;
    uint64_t pendingReady = 0;
    int64_t pendingPts = -1;
    uint64_t sequence = 0;

    // --- producer only ---
    fl::Channel link;
};

namespace {

constexpr uint32_t kMaxPool = fl::kMaxPool;

ComPtr<ID3D11Device> createDevice(const uint8_t luid[8], bool luidValid) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
    ComPtr<IDXGIAdapter1> adapter, pick;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 d;
        adapter->GetDesc1(&d);
        if (!luidValid || memcmp(&d.AdapterLuid, luid, 8) == 0) {
            pick = adapter;
            break;
        }
    }
    ComPtr<ID3D11Device> dev;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_1;
    if (FAILED(D3D11CreateDevice(pick.Get(),
                                 pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                 nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr,
                                 nullptr)))
        return nullptr;
    // Not optional: a user-supplied device without this gives intermittent
    // hangs that look like sync bugs (learned the hard way in M2).
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(dev.As(&mt))) mt->SetMultithreadProtected(TRUE);
    return dev;
}

bool finishDeviceSetup(flChannel* ch) {
    ComPtr<ID3D11DeviceContext> ctx;
    ch->device->GetImmediateContext(&ctx);
    if (FAILED(ctx.As(&ch->ctx))) return false;
    ch->waitEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return ch->waitEvent != nullptr;
}

// Consumer: allocate the ring. Everything the producer will ever draw into is
// created here, because the ring belongs to the consumer.
bool allocateRing(flChannel* ch, uint32_t w, uint32_t h, uint32_t poolSize) {
    ComPtr<ID3D11Device5> dev5;
    if (FAILED(ch->device.As(&dev5))) return false;

    for (uint32_t i = 0; i < poolSize; ++i) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = ch->info.format == FL_FORMAT_NV12 ? DXGI_FORMAT_NV12 : ch->info.format == FL_FORMAT_RGBA8 ? DXGI_FORMAT_R8G8B8A8_UNORM : ch->info.format == FL_FORMAT_RGB10A2 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        // NT handles, not the legacy KMT ones: required for DuplicateHandle
        // into another process and for Vulkan/D3D12 import.
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        auto* img = new flImage();
        if (FAILED(ch->device->CreateTexture2D(&td, nullptr, &img->tex))) {
            delete img;
            return false;
        }
        ComPtr<IDXGIResource1> res;
        if (FAILED(img->tex.As(&res)) ||
            FAILED(res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ |
                                                        DXGI_SHARED_RESOURCE_WRITE,
                                           nullptr, &img->shared))) {
            delete img;
            return false;
        }
        ch->images.push_back(img);
        ch->slotReadyValue.push_back(0);
    }

    return true;
}

// Consumer: fresh fences, both counters back to zero.
//
// Called for every producer, not just the first. A producer's readyValue
// starts at 1, so a SECOND producer on a channel whose fence already sat at 50
// would submit "ready 1" against a fence that reads 50: every consumer wait
// passes instantly and every slot looks free, which is a use-before-write on
// both sides. Receivers reattach constantly - a presenter leaves, the next one
// takes the slot - so this is the normal case, not an edge one.
//
// It bumps the generation for the same reason a reallocation does: the shared
// fence handles are new, so anything that imported them (a Vulkan consumer's
// timeline semaphores) is holding stale ones.
bool resetSync(flChannel* ch) {
    ComPtr<ID3D11Device5> dev5;
    if (FAILED(ch->device.As(&dev5))) return false;
    if (ch->readyShared) CloseHandle(ch->readyShared);
    if (ch->consumedShared) CloseHandle(ch->consumedShared);
    ch->readyShared = ch->consumedShared = nullptr;
    ch->readyFence.Reset();
    ch->consumedFence.Reset();
    if (FAILED(dev5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&ch->readyFence))) ||
        FAILED(dev5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                 IID_PPV_ARGS(&ch->consumedFence))))
        return false;
    if (FAILED(ch->readyFence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                                  &ch->readyShared)) ||
        FAILED(ch->consumedFence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                                     &ch->consumedShared)))
        return false;
    for (auto& v : ch->slotReadyValue) v = 0;
    ch->consumedValue = 0;
    ch->readyValue = 0;
    {
        std::lock_guard<std::mutex> hold(ch->pendingMutex);
        ch->hasPending = false; // announced against the old fence
        ch->heldReady=0;
    }
    ++ch->info.generation;
    return true;
}

// Consumer: one producer, handshake then pump FrameReady. Spike scope - fan-out
// needs the back-pressure question answered first (docs/FRAMELINK.md).
// Duplicate the current ring into `peerPid` and describe it. Called at attach
// and again after flRequestGeometry reallocates, which is the whole reason it
// is a function rather than inline handshake code.
bool handOverRing(flChannel* ch, fl::Channel& link, uint32_t peerPid) {
    HANDLE peerProc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, peerPid);
    if (!peerProc) {
        FL_LOG_ERR("framelink: OpenProcess(pid %u) failed", peerPid);
        return false;
    }
    fl::RingDescMsg pd{};
    pd.generation = ch->info.generation;
    pd.width = ch->info.width;
    pd.height = ch->info.height;
    pd.format = (uint32_t)ch->info.format;
    pd.poolSize = (uint32_t)ch->images.size();
    bool ok = true;
    for (size_t i = 0; i < ch->images.size() && ok; ++i) {
        HANDLE dup = nullptr;
        ok = DuplicateHandle(GetCurrentProcess(), ch->images[i]->shared, peerProc, &dup, 0,
                             FALSE, DUPLICATE_SAME_ACCESS) != 0;
        pd.texture[i] = (uint64_t)(uintptr_t)dup;
    }
    HANDLE dupReady = nullptr, dupConsumed = nullptr;
    ok = ok &&
         DuplicateHandle(GetCurrentProcess(), ch->readyShared, peerProc, &dupReady, 0, FALSE,
                         DUPLICATE_SAME_ACCESS) &&
         DuplicateHandle(GetCurrentProcess(), ch->consumedShared, peerProc, &dupConsumed, 0,
                         FALSE, DUPLICATE_SAME_ACCESS);
    pd.readyFence = (uint64_t)(uintptr_t)dupReady;
    pd.consumedFence = (uint64_t)(uintptr_t)dupConsumed;
    CloseHandle(peerProc);
    if (!ok) {
        FL_LOG_ERR("framelink: DuplicateHandle into producer failed");
        return false;
    }
    return link.send(fl::MsgType::RingDesc, pd);
}

void acceptLoop(flChannel* ch) {
    bool first = true;
    while (!ch->stop.load()) {
        auto ready = [](void* context, bool ok) {
            static_cast<flChannel*>(context)->listenerReady.set_value(ok);
        };
        fl::Channel link = fl::Channel::listen(ch->name, first ? +ready : nullptr, ch);
        first = false;
        if (!link.connected()) return;
        if (ch->stop.load()) return;

        HANDLE peerProc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, link.peerPid());
        if (!peerProc) {
            FL_LOG_ERR("framelink: OpenProcess(pid %u) failed", link.peerPid());
            continue;
        }

        // Version first, before a single handle is duplicated into another
        // process: a mismatched peer must be refused, not handed buffers it
        // might interpret differently.
        fl::HelloMsg hello{};
        hello.version = FL_VERSION;
        hello.pid = GetCurrentProcessId();
        hello.luidValid = 1;
        memcpy(hello.adapterLUID, ch->info.adapterLUID, 8);
        if (!link.send(fl::MsgType::Hello, hello)) {
            CloseHandle(peerProc);
            continue;
        }
        auto peerHello = link.recv();
        const auto* ph = peerHello ? peerHello->as<fl::HelloMsg>() : nullptr;
        if (!ph || ph->version != FL_VERSION) {
            FL_LOG_ERR("framelink: refusing producer pid %u - speaks version %u, we speak %u",
                       link.peerPid(), ph ? ph->version : 0u, (unsigned)FL_VERSION);
            CloseHandle(peerProc);
            continue; // closing the link IS the refusal
        }

        // Only ever a can-we-duplicate-into-it precheck; handOverRing opens its
        // own. Leaking it here cost one process handle per attach, and
        // receivers attach and detach for a living.
        CloseHandle(peerProc);

        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            if(ch->frameHeld || ch->consumedFence->GetCompletedValue()<ch->heldReady ||
               (ch->hasPending && ch->readyFence->GetCompletedValue()<ch->pendingReady)) {
                link.send(fl::MsgType::Busy);
                continue;
            }
        }
        if (!resetSync(ch) || !handOverRing(ch, link, link.peerPid())) continue;

        ch->peerGone = false;
        FL_LOG("framelink: producer pid %u attached to '%s'", link.peerPid(),
                ch->name.c_str());

        for (;;) {
            auto msg = link.recv();
            if (!msg || msg->type == fl::MsgType::Bye) break;
            if (ch->stop.load()) return; // flClose cancelled our I/O - leave now
            if (msg->type == fl::MsgType::RequestGeometry) {
                const auto* g = msg->as<fl::GeometryMsg>();
                // Honour it only when it actually differs and is sane. A
                // refusal is silent by design: the producer re-reads flQuery
                // and adapts, so there is nothing for it to handle.
                if (g && g->format == (uint32_t)ch->info.format && fl::validGeometry(g->width,g->height,ch->info.format) &&
                    (g->width != ch->info.width || g->height != ch->info.height)) {
                    // Scoped, not held across the rebuild: resetSync takes the
                    // same mutex, and std::mutex is not recursive. Clearing
                    // hasPending here is enough - only this thread ever sets
                    // it, so nothing can be announced against the old ring
                    // between here and the new ring being live.
                    {
                        std::lock_guard<std::mutex> hold(ch->pendingMutex);
                        if(ch->frameHeld || ((ch->flags & (FL_GPU_SYNC|FL_DEFER_RELEASE)) && ch->consumedFence->GetCompletedValue()<ch->heldReady)){if(!handOverRing(ch,link,link.peerPid()))break;continue;}
                        ch->hasPending = false; // stale: the old ring is going away
                    }
                    const uint32_t pool = (uint32_t)ch->images.size();
                    for (flImage* img : ch->images) {
                        if (img->shared) CloseHandle(img->shared);
                        delete img;
                    }
                    ch->images.clear();
                    ch->slotReadyValue.clear();
                    ch->info.width = g->width;
                    ch->info.height = g->height;
                    if (!allocateRing(ch, g->width, g->height, pool) || !resetSync(ch)) {
                        FL_LOG_ERR("framelink: reallocation to %ux%u failed", g->width,
                                   g->height);
                        break;
                    }
                    FL_LOG("framelink: '%s' reallocated to %ux%u at the producer's request",
                           ch->name.c_str(), g->width, g->height);
                }
                if (!handOverRing(ch, link, link.peerPid())) break;
                continue;
            }
            if (msg->type != fl::MsgType::FrameReady) continue;
            const auto* fr = msg->as<fl::FrameReadyMsg>();
            if (!fr || fr->slot >= ch->images.size()) continue;
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            // Latest-frame-wins: a slow consumer must never stall the producer.
            if(fltrace::enabled()) {
                fltrace::Event e{"ipc.frame_received",fltrace::clockNs(),0,fltrace::id(ch->name.c_str()),
                    ch->sequence+1,fr->readyValue,fr->slot,fr->ptsNs,ch->hasPending?1:0};
                fltrace::record(e); // result=1: unconsumed pending frame replaced
            }
            ch->hasPending = true;
            ch->pendingSlot = fr->slot;
            ch->pendingReady = fr->readyValue;
            ch->pendingPts = fr->ptsNs;
            ++ch->sequence;
        }
        ch->peerGone = true;
        FL_LOG("framelink: producer detached from '%s'", ch->name.c_str());
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

FL_API flResult flCreateChannelOnAdapter(const char* name, uint32_t width, uint32_t height,
                                         flFormat format, uint32_t poolSize, uint32_t flags,
                                         const uint8_t adapterLUID[8], flChannel** out) {
    fltrace::Scope trace("flCreateChannelOnAdapter",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=(uint64_t)format;trace.event.slot=poolSize;trace.event.pts=flags;}

    if (!name || !out || !fl::validGeometry(width,height,format)) return trace.result(FL_INVALID);
    *out = nullptr;
    if (format != FL_FORMAT_BGRA8 && format != FL_FORMAT_RGBA8 && format != FL_FORMAT_RGB10A2 && format != FL_FORMAT_NV12) return trace.result(FL_FORMAT_UNSUPPORTED);
    if (poolSize == 0 || poolSize > kMaxPool) return trace.result(FL_INVALID);

    auto* ch = new flChannel();
    ch->isConsumer = true;
    ch->flags = flags;
    ch->name = name;
    ch->device = createDevice(adapterLUID, adapterLUID != nullptr);
    if (!ch->device || !finishDeviceSetup(ch)) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }

    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC ad{};
    if (SUCCEEDED(ch->device.As(&dxgiDev)) && SUCCEEDED(dxgiDev->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetDesc(&ad)))
        memcpy(ch->info.adapterLUID, &ad.AdapterLuid, 8);

    ch->info.version = FL_VERSION;
    ch->info.width = width;
    ch->info.height = height;
    ch->info.format = format;
    ch->info.poolSize = poolSize;

    if (!allocateRing(ch, width, height, poolSize) || !resetSync(ch)) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }
    // Success must mean the endpoint exists, not merely that its thread was spawned.
    // Producers intentionally fail fast for absent channels.
    auto ready = ch->listenerReady.get_future();
    ch->accept = std::thread(acceptLoop, ch);
    if (!ready.get()) { flClose(ch); return trace.result(FL_INTERNAL); }
    *out = ch;
    return trace.result(FL_OK);
}

FL_API flResult flCreateChannelEx(const char* name, uint32_t width, uint32_t height,
                                  flFormat format, uint32_t poolSize, uint32_t flags,
                                  flChannel** out) {
    fltrace::Scope trace("flCreateChannelEx",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=(uint64_t)format;trace.event.slot=poolSize;trace.event.pts=flags;}

    return trace.result(flCreateChannelOnAdapter(name, width, height, format, poolSize, flags, nullptr, out));
}

FL_API flResult flCreateChannel(const char* name, uint32_t width, uint32_t height,
                                flFormat format, uint32_t poolSize, flChannel** out) {
    return flCreateChannelEx(name, width, height, format, poolSize, 0, out);
}

FL_API flResult flOpenProducer(const char* name, flChannel** out) {
    fltrace::Scope trace("flOpenProducer",fltrace::id(name));

    if (!name || !out) return trace.result(FL_INVALID);
    auto* ch = new flChannel();
    ch->isConsumer = false;
    ch->name = name;
    // Short timeout and no retry: a producer attaches to a channel that exists
    // or it does not. It never creates one.
    bool busy = false;
    // Short timeout: a producer attaches to a channel or it does not. Walking a
    // set of slots must not take seconds per slot.
    ch->link = fl::Channel::connect(name, 250, &busy);
    if (!ch->link.connected()) {
        delete ch;
        return trace.result(busy ? FL_BUSY : FL_NOT_FOUND);
    }

    // The consumer speaks first, so a version mismatch is caught before it
    // duplicates any handle to us.
    auto helloMsg = ch->link.recv();
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
    mine.pid = GetCurrentProcessId();
    if (!ch->link.send(fl::MsgType::Hello, mine)) {
        delete ch;
        return trace.result(FL_INTERNAL);
    }
    // Open on the consumer's adapter. Getting this wrong is the cross-adapter
    // failure the API promises to refuse rather than crash on.
    ch->device = createDevice(srv->adapterLUID, srv->luidValid != 0);
    if (!ch->device || !finishDeviceSetup(ch)) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }
    memcpy(ch->info.adapterLUID, srv->adapterLUID, 8);

    auto poolMsg = ch->link.recv();
    if(poolMsg && poolMsg->type==fl::MsgType::Busy){flClose(ch);return trace.result(FL_BUSY);}
    if (!poolMsg || poolMsg->type != fl::MsgType::RingDesc) {
        flClose(ch);
        return trace.result(FL_DISCONNECTED);
    }
    const auto* pd = poolMsg->as<fl::RingDescMsg>();
    if (!pd || pd->poolSize == 0 || pd->poolSize > kMaxPool) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }

    ComPtr<ID3D11Device1> dev1;
    ComPtr<ID3D11Device5> dev5;
    if (FAILED(ch->device.As(&dev1)) || FAILED(ch->device.As(&dev5))) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }
    for (uint32_t i = 0; i < pd->poolSize; ++i) {
        auto* img = new flImage();
        img->producer = true;
        img->shared = (HANDLE)(uintptr_t)pd->texture[i];
        if (FAILED(dev1->OpenSharedResource1(img->shared, IID_PPV_ARGS(&img->tex)))) {
            delete img;
            flClose(ch);
            return trace.result(FL_ADAPTER_MISMATCH);
        }
        ch->images.push_back(img);
        ch->slotReadyValue.push_back(0);
    }
    // Keep the duplicated handles, not just the opened fences: a producer that
    // draws with Vulkan or D3D12 needs the raw ready-fence handle to import as
    // its own timeline semaphore (flSharedReadyFence). Dropping them here made
    // flSharedReadyFence return NULL, which only showed up once the renderer -
    // the first non-D3D11 producer - tried to use it.
    ch->readyShared = (HANDLE)(uintptr_t)pd->readyFence;
    ch->consumedShared = (HANDLE)(uintptr_t)pd->consumedFence;
    if (FAILED(dev5->OpenSharedFence(ch->readyShared, IID_PPV_ARGS(&ch->readyFence))) ||
        FAILED(dev5->OpenSharedFence(ch->consumedShared, IID_PPV_ARGS(&ch->consumedFence)))) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }

    ch->info.version = FL_VERSION;
    ch->info.generation = pd->generation;
    ch->info.width = pd->width;
    ch->info.height = pd->height;
    ch->info.format = (flFormat)pd->format;
    ch->info.poolSize = pd->poolSize;
    *out = ch;
    return trace.result(FL_OK);
}

// Producer: replace our view of the ring from a RingDesc. Used at attach and
// again whenever the consumer reallocates.
static flResult importRing(flChannel* ch, const fl::RingDescMsg* pd) {
    ComPtr<ID3D11Device1> dev1;
    ComPtr<ID3D11Device5> dev5;
    if (FAILED(ch->device.As(&dev1)) || FAILED(ch->device.As(&dev5))) return FL_INTERNAL;
    for (flImage* img : ch->images) {
        if (img->shared) CloseHandle(img->shared);
        delete img;
    }
    ch->images.clear();
    ch->slotReadyValue.clear();
    for (uint32_t i = 0; i < pd->poolSize; ++i) {
        auto* img = new flImage();
        img->producer = true;
        img->shared = (HANDLE)(uintptr_t)pd->texture[i];
        if (FAILED(dev1->OpenSharedResource1(img->shared, IID_PPV_ARGS(&img->tex)))) {
            delete img;
            return FL_ADAPTER_MISMATCH;
        }
        ch->images.push_back(img);
        ch->slotReadyValue.push_back(0);
    }
    ch->readyShared = (HANDLE)(uintptr_t)pd->readyFence;
    ch->consumedShared = (HANDLE)(uintptr_t)pd->consumedFence;
    if (FAILED(dev5->OpenSharedFence(ch->readyShared, IID_PPV_ARGS(&ch->readyFence))) ||
        FAILED(dev5->OpenSharedFence(ch->consumedShared, IID_PPV_ARGS(&ch->consumedFence))))
        return FL_INTERNAL;
    ch->info.generation = pd->generation;
    ch->info.width = pd->width;
    ch->info.height = pd->height;
    ch->info.format = (flFormat)pd->format;
    ch->info.poolSize = pd->poolSize;
    return FL_OK;
}

FL_API flResult flRequestGeometry(flChannel* ch, uint32_t width, uint32_t height,
                                  flFormat format) {
    fltrace::Scope trace("flRequestGeometry", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !fl::validGeometry(width,height,format)) return trace.result(FL_INVALID);
    if(format != ch->info.format) return trace.result(FL_FORMAT_UNSUPPORTED);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    fl::GeometryMsg g{};
    g.width = width;
    g.height = height;
    g.format = (uint32_t)format;
    if (!ch->link.send(fl::MsgType::RequestGeometry, g)) return trace.result(FL_DISCONNECTED);
    // The consumer always answers with a RingDesc - the current one if it
    // declined - so the producer's view is never left stale.
    auto reply = ch->link.recv();
    const auto* pd = reply && reply->type == fl::MsgType::RingDesc
                         ? reply->as<fl::RingDescMsg>()
                         : nullptr;
    if (!pd || pd->poolSize == 0 || pd->poolSize > kMaxPool) return trace.result(FL_DISCONNECTED);
    ch->readyValue = 0; // fences are fresh with the new ring
    return trace.result(importRing(ch, pd));
}

FL_API flResult flAcquireBuffer(flChannel* ch, flBuffer* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireBuffer", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !out) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    const DWORD deadline = GetTickCount() + timeoutMs;
    for (;;) {
        const uint64_t consumed = ch->consumedFence->GetCompletedValue();
        for (size_t i = 0; i < ch->images.size(); ++i) {
            // Free once the consumer has promised never to read this slot's
            // previous contents again.
            if (ch->slotReadyValue[i] <= consumed) {
                ch->slotReadyValue[i] = UINT64_MAX; // reserve until submit
                out->slot = (uint32_t)i;
                out->image = ch->images[i];
                if(trace.active){trace.event.slot=out->slot;}
                return trace.result(FL_OK);
            }
        }
        // A dead consumer cannot advance this fence. Detect it even when no
        // free slot remains and hence no submit/write would see broken pipe.
        if (!ch->link.peerAlive()) return trace.result(FL_DISCONNECTED);
        const DWORD now = GetTickCount();
        if (now >= deadline) return trace.result(FL_TIMEOUT);
        uint64_t oldest = UINT64_MAX;
        for (uint64_t v : ch->slotReadyValue)
            if (v > consumed && v < oldest) oldest = v;
        if (oldest == UINT64_MAX) { Sleep(1); continue; }
        ch->consumedFence->SetEventOnCompletion(oldest, ch->waitEvent);
        fltrace::call("wait.consumed_fence", [&]{return WaitForSingleObject(ch->waitEvent, deadline - now);});
    }
}

FL_API flResult flSubmit(flChannel* ch, const flBuffer* buf, int64_t ptsNs) {
    fltrace::Scope trace("flSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));
    if(trace.active) { trace.event.pts=ptsNs; trace.event.slot=buf?buf->slot:0; }

    uint64_t value = 0;
    flResult r = flBeginSubmit(ch, buf, &value);
    if (r != FL_OK) return trace.result(r);
    flImageUnmap(buf->image);
    // Correct only because the caller drew with THIS device's context.
    {
        fltrace::Scope signalTrace("d3d11.signal_ready", fltrace::id(ch->name.c_str()));
        if(signalTrace.active) { signalTrace.event.pts=ptsNs; signalTrace.event.slot=buf->slot; signalTrace.event.fence=value; }
        signalTrace.result(ch->ctx->Signal(ch->readyFence.Get(), value));
    }
    {
        // Keep the required flush. These optional wall-time scopes distinguish
        // driver submission latency from IPC/consumer work; a long span alone
        // cannot distinguish a blocked driver call from thread descheduling.
        fltrace::Scope flushTrace("d3d11.flush_ready", fltrace::id(ch->name.c_str()));
        if(flushTrace.active) { flushTrace.event.pts=ptsNs; flushTrace.event.slot=buf->slot; flushTrace.event.fence=value; }
        ch->ctx->Flush(); // no Flush after Signal == intermittent hangs (M2)
    }
    return trace.result(flEndSubmit(ch, buf, value, ptsNs));
}

FL_API flResult flSharedReadyFence(flChannel* ch, void** handle) {
    if (!ch || !handle) return FL_INVALID;
    // Both roles need it now: producers signal it, and an FL_GPU_SYNC consumer
    // waits on it in its own submit instead of blocking a thread.
    *handle = ch->readyShared;
    return *handle ? FL_OK : FL_INTERNAL;
}

FL_API flResult flSharedConsumedFence(flChannel* ch, void** handle) {
    if (!ch || !handle) return FL_INVALID;
    *handle = ch->consumedShared;
    return *handle ? FL_OK : FL_INTERNAL;
}

FL_API flResult flBeginSubmit(flChannel* ch, const flBuffer* buf, uint64_t* signalValue) {
    fltrace::Scope trace("flBeginSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !buf || !signalValue || buf->slot >= ch->images.size())
        return trace.result(FL_INVALID);
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
    ch->slotReadyValue[buf->slot] = signalValue;

    fl::FrameReadyMsg fr{};
    fr.slot = buf->slot;
    fr.readyValue = signalValue;
    fr.ptsNs = ptsNs;
    return trace.result(ch->link.send(fl::MsgType::FrameReady, fr) ? FL_OK : FL_DISCONNECTED);
}

FL_API flResult flAcquireFrame(flChannel* ch, flFrame* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireFrame", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || !ch->isConsumer || !out) return trace.result(FL_INVALID);
    const DWORD deadline = GetTickCount() + timeoutMs;
    for (;;) {
        uint32_t slot = 0;
        uint64_t ready = 0;
        int64_t pts = -1;
        uint64_t seq = 0;
        bool got = false;
        {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            if (ch->hasPending && !ch->frameHeld &&
                ((ch->flags & FL_GPU_SYNC) || ch->readyFence->GetCompletedValue() >= ch->pendingReady)) {
                ch->frameHeld = true;
                ch->heldReady = ch->pendingReady;
                ch->heldSlot = ch->pendingSlot;
                ch->hasPending = false;
                slot = ch->pendingSlot;
                ready = ch->pendingReady;
                pts = ch->pendingPts;
                seq = ch->sequence;
                got = true;
            }
        }
        if (got) {
            // The producer's GPU writes are not necessarily done yet; the fence
            // is what makes this zero-copy AND correct. An FL_GPU_SYNC consumer
            // waits on that fence inside its own submit, so blocking here would
            // just be a stall it did not ask for - a compositor doing this a
            // dozen times per frame is exactly the latency regression to avoid.
            out->slot = slot;
            out->image = ch->images[slot];
            out->ptsNs = pts;
            out->sequence = seq;
            out->_readyValue = ready;
            if(trace.active){trace.event.slot=out->slot;trace.event.sequence=out->sequence;trace.event.pts=out->ptsNs;trace.event.fence=out->_readyValue;}
                return trace.result(FL_OK);
        }
        if (ch->peerGone.load()) return trace.result(FL_DISCONNECTED);
        const DWORD now = GetTickCount();
        if (now >= deadline) return trace.result(FL_TIMEOUT);
        Sleep(1);
    }
}

FL_API flResult flRelease(flChannel* ch, const flFrame* frame) {
    fltrace::Scope trace("flRelease", fltrace::id(ch ? ch->name.c_str() : nullptr));
    if(trace.active && frame) {trace.event.sequence=frame->sequence;trace.event.pts=frame->ptsNs;trace.event.fence=frame->_readyValue;trace.event.slot=frame->slot;}

    if (!ch || !ch->isConsumer || !frame || frame->slot >= ch->images.size()) return trace.result(FL_INVALID);
    {
        std::lock_guard<std::mutex> hold(ch->pendingMutex);
        if(!ch->frameHeld || frame->_readyValue!=ch->heldReady || frame->slot!=ch->heldSlot)
            return trace.result(FL_INVALID);
        ch->frameHeld=false;
    }
    // FL_GPU_SYNC: the consumer signals the consumed fence itself, ordered
    // after its own reads. Signalling here from framelink's context would tell
    // the producer the slot is free while the consumer's GPU is still sampling
    // it. Keep the bookkeeping, skip the signal.
    if (ch->flags & (FL_GPU_SYNC|FL_DEFER_RELEASE)) {
        if (frame->_readyValue > ch->consumedValue) ch->consumedValue = frame->_readyValue;
        return trace.result(FL_OK);
    }
    // Signal THIS frame's value, not the newest one seen. Using the newest
    // (which an earlier cut of this did) tells the producer that slots are free
    // while we may still be reading them - a latent use-while-writing race that
    // the readback test happened not to trip because it copies out immediately.
    //
    // Still monotonic: the producer treats consumedFence as "everything up to
    // here is free", so it must never go backwards.
    if (frame->_readyValue <= ch->consumedValue) return trace.result(FL_OK); // already released
    const uint64_t value = frame->_readyValue;
    ch->consumedValue = value;
    ch->ctx->Signal(ch->consumedFence.Get(), value);
    ch->ctx->Flush();
    return trace.result(FL_OK);
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
        // Unblock ConnectNamedPipe in the accept thread by dialling ourselves.
        { fl::Channel poke = fl::Channel::connect(ch->name, 200); }
        // The poke only reaches an accept thread WAITING for a producer. One
        // attached to a LIVE producer is blocked in recv() on the peer pipe,
        // which no poke touches - and a healthy producer streaming at 30 fps
        // keeps that recv fed forever, so join() would hang the closing
        // thread. Found by the moderator's kick: the first consumer-side
        // close with a producer mid-stream froze the render loop solid.
        // CancelSynchronousIo aborts the blocked ReadFile; retried, because
        // the thread may be between I/Os when the cancel lands.
        if (ch->accept.joinable()) {
            HANDLE th = (HANDLE)ch->accept.native_handle();
            for (int i = 0; i < 500; ++i) {
                if (WaitForSingleObject(th, 10) == WAIT_OBJECT_0) break;
                CancelSynchronousIo(th);
            }
            ch->accept.join();
        }
    } else if (ch->link.connected()) {
        ch->link.send(fl::MsgType::Bye);
    }
    for (flImage* img : ch->images) {
        if (img->shared) CloseHandle(img->shared);
        delete img;
    }
    if (ch->readyShared) CloseHandle(ch->readyShared);
    if (ch->consumedShared) CloseHandle(ch->consumedShared);
    if (ch->waitEvent) CloseHandle(ch->waitEvent);
    delete ch;
}

// ---- D3D11 interop -----------------------------------------------------------

FL_API flResult flImageMapPlanes(flImage* img, flMappedImage* out) {
    fltrace::Scope trace("flImageMapPlanes", (uint64_t)(uintptr_t)img);
    if (!img || !out) return trace.result(FL_INVALID);
    *out = {};
    D3D11_TEXTURE2D_DESC desc{};
    img->tex->GetDesc(&desc);
    if (!img->mapped.pData) {
        if (!img->staging) {
            ComPtr<ID3D11Device> dev;
            img->tex->GetDevice(&dev);
            dev->GetImmediateContext(&img->mapContext);
            auto staging = desc;
            staging.Usage = D3D11_USAGE_STAGING;
            staging.BindFlags = staging.MiscFlags = 0;
            staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateTexture2D(&staging, nullptr, &img->staging)))
                return trace.result(FL_FORMAT_UNSUPPORTED);
        }
        img->mapContext->CopyResource(img->staging.Get(), img->tex.Get());
        if (FAILED(img->mapContext->Map(img->staging.Get(), 0, D3D11_MAP_READ_WRITE, 0, &img->mapped)))
            return trace.result(FL_INTERNAL);
    }
    bool nv12 = desc.Format == DXGI_FORMAT_NV12;
    out->planes = nv12 ? 2 : 1;
    out->plane[0] = {img->mapped.pData, img->mapped.RowPitch, desc.Width * (nv12 ? 1u : 4u), desc.Height};
    if (nv12) out->plane[1] = {(uint8_t*)img->mapped.pData + (size_t)img->mapped.RowPitch * desc.Height,
                              img->mapped.RowPitch, desc.Width, desc.Height / 2};
    return trace.result(FL_OK);
}
FL_API flResult flImageMap(flImage* img, void** pixels, uint64_t* stride) {
    if (!img || !pixels) return FL_INVALID;
    D3D11_TEXTURE2D_DESC desc{}; img->tex->GetDesc(&desc);
    if (desc.Format == DXGI_FORMAT_NV12) return FL_FORMAT_UNSUPPORTED;
    flMappedImage view{};
    flResult r = flImageMapPlanes(img, &view);
    if (r == FL_OK) { *pixels = view.plane[0].pixels; if (stride) *stride = view.plane[0].stride; }
    return r;
}
FL_API void flImageUnmap(flImage* img) {
    fltrace::Scope trace("flImageUnmap", (uint64_t)(uintptr_t)img);
    if (!img || !img->mapped.pData) return;
    img->mapContext->Unmap(img->staging.Get(), 0);
    img->mapped = {};
    if (img->producer) img->mapContext->CopyResource(img->tex.Get(), img->staging.Get());
}

FL_API ID3D11Texture2D* flImageD3D11(flImage* img) { return img ? img->tex.Get() : nullptr; }
FL_API ID3D11Device* flChannelD3D11Device(flChannel* ch) {
    return ch ? ch->device.Get() : nullptr;
}
FL_API void* flImageSharedHandle(flImage* img) { return img ? img->shared : nullptr; }

} // extern "C"
