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
#include "framelink_memory.h"
#include "fl_read_ledger.hpp"
#include "fl_pending.hpp"
#include "fl_buffer_refs.hpp"
#include "fl_arrival.hpp"
#include "fl_layout.h"
#include "framelink_d3d11.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <future>
#include <memory>

#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fl_ipc.h"
#include "fl_log.h"
#include "fl_protocol.h"
#include "fl_metadata.hpp"

using Microsoft::WRL::ComPtr;

struct flD3D11DeviceOwner {
    std::atomic<uint32_t> references{1};
    std::mutex mutex;
    bool attached=false, poisoned=false;
    uint8_t luid[8]{};
    ComPtr<ID3D11Fence> retiringFence;
    uint64_t retiringValue=0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext4> context;
};

// Shared by the channel and snapshots. Last reference releases attachment only
// after channel resources and snapshot handles are destroyed.
struct DeviceAttachment {
    flD3D11DeviceOwner* owner=nullptr;
    ~DeviceAttachment() {
        if(!owner)return;
        { std::lock_guard<std::mutex> lock(owner->mutex); owner->attached=false; }
        flReleaseD3D11DeviceOwner(owner);
    }
};

struct flImage {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<ID3D11DeviceContext> mapContext;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    ~flImage() { if(mapped.pData && mapContext && staging) mapContext->Unmap(staging.Get(),0); }
    bool producer = false;
    HANDLE shared = nullptr; // borrowed by callers; owned here
};

struct LeaseCompletion {
    uint64_t token=0,value=0;
    ComPtr<ID3D11Fence> fence;
};

struct flD3D11PoolSnapshot {
    std::shared_ptr<DeviceAttachment> attachment;
    flChannelInfo info{};
    std::vector<HANDLE> handles;
    std::shared_ptr<std::atomic<uint32_t>> pins;
    ~flD3D11PoolSnapshot() {
        for(HANDLE handle:handles)if(handle)CloseHandle(handle);
        if(pins)--*pins;
    }
};

struct flChannel {
    std::shared_ptr<DeviceAttachment> attachment; // destroyed last
    bool isConsumer = false;
    bool externalPool = false;
    uint32_t flags = 0;
    uint32_t peerFeatures=0;
    fl::BufferReferences bufferReferences;
    flFrameMetadata slotMetadata[FL_MAX_POOL_SIZE]{};
    flFrameMetadata heldMetadata{};
    std::string name;
    std::recursive_mutex poolMutex; // snapshot versus ring/epoch replacement
    std::shared_ptr<std::atomic<uint32_t>> poolPins = std::make_shared<std::atomic<uint32_t>>(0);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext4> ctx;
    ComPtr<ID3D11Fence> readyFence, consumedFence;
    HANDLE readyShared = nullptr, consumedShared = nullptr;
    HANDLE waitEvent = nullptr;

    std::vector<flImage*> images;
    std::vector<uint64_t> slotReadyValue; // producer: last value signalled per slot
    uint32_t submittingSlot = UINT32_MAX; // exactly one begin/end transaction
    uint64_t submittingValue = 0;
    uint64_t readyValue = 0;              // producer: monotonic submit counter
    uint64_t consumedValue = 0;           // consumer: monotonic release counter

    flChannelInfo info{};

    // --- consumer only ---
    fl::Channel peer;
    std::thread accept;
    std::promise<bool> listenerReady;
    std::atomic<bool> stop{false};
    std::atomic<bool> peerGone{false};
    // A fence completed with an error or the device reports removed (decision 13).
    // Sticky: every later frame or lease call returns FL_DEVICE_LOST.
    std::atomic<bool> deviceLost{false};
    std::mutex pendingMutex;
    fl::PendingFrames pending; // announced, not yet acquired
    fl::ReadLedger readers;
    std::array<LeaseCompletion,fl::ReadLedger::MaxReaders> leaseCompletions;
    bool frameHeld = false;
    uint64_t heldReady = 0;
    uint32_t heldSlot = 0;
    uint64_t heldArrival=0;
    uint64_t sequence = 0;

    // --- producer only ---
    fl::Channel link;
};

namespace {

// Test-only fault injection (M6 decision 13, B2). FRAMELINK_TEST_FAULTS is
// read once per process; unset (the normal case) it does nothing.
// device-removed-after=N: once consumers in this process have acquired N
// frames, every fence reads UINT64_MAX and every device reports removed, as
// after a TDR or adapter removal.
std::atomic<uint64_t> g_framesAcquired{0};
uint64_t testDeviceRemovedAfter() {
    static const uint64_t after=[]{
        unsigned long long v=0;const char* env=getenv("FRAMELINK_TEST_FAULTS");
        for(const char* p=env;p && *p;){const char* end=strchr(p,',');if(!end)end=p+strlen(p);
            if(sscanf(p,"device-removed-after=%llu",&v)==1)return uint64_t(v);p=*end?end+1:end;}
        return uint64_t(0);
    }();
    return after;
}
bool testDeviceRemoved() {const uint64_t n=testDeviceRemovedAfter();return n && g_framesAcquired.load()>=n;}
// Every fence read and device-removed check goes through these.
template<class F> uint64_t completedValue(const F& fence) {return testDeviceRemoved()?UINT64_MAX:fence->GetCompletedValue();}
template<class D> bool deviceRemoved(const D& device) {return testDeviceRemoved() || FAILED(device->GetDeviceRemovedReason());}
// A shared fence reads UINT64_MAX when ANY device holding it goes away: our
// own device's removal (device loss) or the peer process's death (measured:
// the producer-kill fault test). Only our own removed device is device loss.
template<class D> flResult fenceLost(const D& device) {return device && deviceRemoved(device)?FL_DEVICE_LOST:FL_DISCONNECTED;}

constexpr uint32_t kMaxPool = fl::kMaxPool;
// Windows-only optional capability. Requiring it selects pull duplication,
// so a cancelled setup cannot strand handles pushed into the caller process.
constexpr uint32_t kPullRingFeature = 0x8000u;

struct SetupDeadline {
    HANDLE thread = nullptr, cancel = nullptr;
    ULONGLONG deadline = 0;
    std::atomic<bool> done{false};
    std::atomic<int> failure{FL_OK};
    std::thread watcher;
    SetupDeadline(uint32_t timeout, HANDLE event) : cancel(event), deadline(GetTickCount64()+timeout) {
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                             &thread, THREAD_TERMINATE, FALSE, 0)) { failure=FL_INTERNAL; return; }
        watcher=std::thread([this] {
            while (!done.load()) {
                check();
                if (failure.load()!=FL_OK) CancelSynchronousIo(thread);
                Sleep(1);
            }
        });
    }
    flResult check() {
        if (failure.load()!=FL_OK) return (flResult)failure.load();
        DWORD state=cancel?WaitForSingleObject(cancel,0):WAIT_TIMEOUT;
        int result=state==WAIT_OBJECT_0?FL_DISCONNECTED:
            state==WAIT_FAILED?FL_INVALID:GetTickCount64()>=deadline?FL_TIMEOUT:FL_OK;
        if(result!=FL_OK) { int expected=FL_OK; failure.compare_exchange_strong(expected,result); }
        return (flResult)failure.load();
    }
    void finish() { done=true; if(watcher.joinable())watcher.join(); }
    ~SetupDeadline() { finish(); if(thread)CloseHandle(thread); }
};

ComPtr<ID3D11Device> createDevice(const uint8_t luid[8], bool luidValid, bool* adapterMissing = nullptr) {
    if(adapterMissing)*adapterMissing=false;
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
    // An explicitly selected adapter must never fall back to the default GPU.
    if (luidValid && !pick) { if(adapterMissing)*adapterMissing=true; return nullptr; }
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

flResult attachDevice(flChannel* ch, flD3D11DeviceOwner* owner,
                      const uint8_t* luid, bool checkLuid) {
    auto attachment=std::make_shared<DeviceAttachment>();
    std::lock_guard<std::mutex> lock(owner->mutex);
    // A removed device is device loss (decision 13); a pool poisoned for an
    // unsafe retirement on a live device stays a disconnect.
    if(deviceRemoved(owner->device)) {owner->poisoned=true;return FL_DEVICE_LOST;}
    if(owner->poisoned)return FL_DISCONNECTED;
    if(checkLuid && (!luid || memcmp(luid,owner->luid,8)))return FL_ADAPTER_MISMATCH;
    if(owner->attached)return FL_BUSY;
    if(owner->retiringFence) {
        const auto completed=completedValue(owner->retiringFence);
        if(completed==UINT64_MAX){owner->poisoned=true;return fenceLost(owner->device);}
        if(completed<owner->retiringValue)return FL_BUSY;
        owner->retiringFence.Reset();owner->retiringValue=0;
    }
    flRetainD3D11DeviceOwner(owner);
    attachment->owner=owner; owner->attached=true;
    ch->attachment=std::move(attachment);
    ch->device=owner->device; ch->ctx=owner->context;
    return FL_OK;
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

// Adopt, do not allocate/copy. One independently owned NT reference per image.
flResult adoptRing(flChannel* ch, void* const* handles, uint32_t count) {
    using CompareHandles = BOOL (WINAPI*)(HANDLE,HANDLE);
    auto compare=reinterpret_cast<CompareHandles>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));
    if(!compare)return FL_FORMAT_UNSUPPORTED; // Cannot establish distinct slots.
    ComPtr<ID3D11Device1> dev1;
    if(FAILED(ch->device.As(&dev1)))return FL_INTERNAL;
    const DXGI_FORMAT expected=ch->info.format==FL_FORMAT_NV12?DXGI_FORMAT_NV12:
        ch->info.format==FL_FORMAT_RGBA8?DXGI_FORMAT_R8G8B8A8_UNORM:
        ch->info.format==FL_FORMAT_RGB10A2?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
    for(uint32_t i=0;i<count;i++) {
        if(!handles[i] || handles[i]==INVALID_HANDLE_VALUE)return FL_INVALID;
        for(uint32_t j=0;j<i;j++)
            if(compare(handles[i],handles[j]))return FL_INVALID; // Alias slots overwrite in-flight pixels.
        auto* image=new flImage();ch->images.push_back(image);
        if(!DuplicateHandle(GetCurrentProcess(),handles[i],GetCurrentProcess(),&image->shared,
                            0,FALSE,DUPLICATE_SAME_ACCESS))return FL_INVALID;
        if(FAILED(dev1->OpenSharedResource1(image->shared,IID_PPV_ARGS(&image->tex))))
            return FL_ADAPTER_MISMATCH;
        D3D11_TEXTURE2D_DESC d{};image->tex->GetDesc(&d);
        if(d.Width!=ch->info.width || d.Height!=ch->info.height || d.Format!=expected ||
           d.ArraySize!=1 || d.MipLevels!=1 || d.SampleDesc.Count!=1 ||
           !(d.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))return FL_FORMAT_UNSUPPORTED;
        if((ch->flags & FL_GPU_RENDER) && !(d.BindFlags & D3D11_BIND_RENDER_TARGET))
            return FL_FORMAT_UNSUPPORTED;
        ch->slotReadyValue.push_back(0);
    }
    return FL_OK;
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
    std::lock_guard<std::recursive_mutex> poolLock(ch->poolMutex);
    if(ch->poolPins->load())return false;
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
    // Reconfiguration/reattach only: retire D3D's deferred destruction queue.
    // This is not a GPU completion promise and adds no per-frame flush.
    ch->ctx->Flush();
    for (auto& v : ch->slotReadyValue) v = 0;
    ch->consumedValue = 0;
    ch->readyValue = 0;
    {
        std::lock_guard<std::mutex> hold(ch->pendingMutex);
        ch->pending.clear(); // announced against the old fence
        ch->readers.resetEpoch();ch->heldReady=0;
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
    if (ch->peerFeatures & (kPullRingFeature << 16)) {
        for(size_t i=0;i<ch->images.size();++i)pd.texture[i]=(uint64_t)(uintptr_t)ch->images[i]->shared;
        pd.readyFence=(uint64_t)(uintptr_t)ch->readyShared;
        pd.consumedFence=(uint64_t)(uintptr_t)ch->consumedShared;
        CloseHandle(peerProc);
        return link.send(fl::MsgType::RingDesc,pd);
    }
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
        hello._pad=fl::localFeatures(ch->flags)|kPullRingFeature;
        hello.pid = GetCurrentProcessId();
        hello.luidValid = 1;
        memcpy(hello.adapterLUID, ch->info.adapterLUID, 8);
        if (!link.send(fl::MsgType::Hello, hello)) {
            CloseHandle(peerProc);
            continue;
        }
        auto peerHello = link.recv();
        const auto* ph = peerHello && peerHello->type==fl::MsgType::Hello && peerHello->payload.size()==sizeof(fl::HelloMsg) ? peerHello->as<fl::HelloMsg>() : nullptr;
        if (!ph || ph->version != FL_VERSION || !fl::compatibleFeatures(hello._pad,ph->_pad)) {
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
            std::lock_guard<std::recursive_mutex> poolLock(ch->poolMutex);
            {
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            ch->peerFeatures=ph->_pad;
            if(ch->poolPins->load() || !ch->readers.empty() || ch->frameHeld || completedValue(ch->consumedFence)<ch->heldReady ||
               !ch->pending.all([ch](const fl::PendingFrame& f){return completedValue(ch->readyFence)>=f.ready;})) {
                link.send(fl::MsgType::Busy);
                continue;
            }
        }
        if (!resetSync(ch) || !handOverRing(ch, link, link.peerPid())) continue;
        }

        ch->peerGone = false;
        FL_LOG("framelink: producer pid %u attached to '%s'", link.peerPid(),
                ch->name.c_str());

        for (;;) {
            auto msg = link.recv();
            if (!msg || msg->type == fl::MsgType::Bye) break;
            if (ch->stop.load()) return; // flClose cancelled our I/O - leave now
            if (msg->type == fl::MsgType::RequestGeometry) {
                std::lock_guard<std::recursive_mutex> poolLock(ch->poolMutex);
                const auto* g = msg->as<fl::GeometryMsg>();
                // Honour it only when it actually differs and is sane. A
                // refusal is silent by design: the producer re-reads flQuery
                // and adapts, so there is nothing for it to handle.
                if (!ch->externalPool && !ch->poolPins->load() && g && g->format == (uint32_t)ch->info.format && fl::validGeometry(g->width,g->height,ch->info.format) &&
                    (g->width != ch->info.width || g->height != ch->info.height)) {
                    // Scoped, not held across the rebuild: resetSync takes the
                    // same mutex, and std::mutex is not recursive. Clearing
                    // the pending queue here is enough - only this thread ever sets
                    // it, so nothing can be announced against the old ring
                    // between here and the new ring being live.
                    {
                        std::lock_guard<std::mutex> hold(ch->pendingMutex);
                        if(!ch->readers.empty() || ch->frameHeld || ((ch->flags & (FL_GPU_SYNC|FL_DEFER_RELEASE)) && completedValue(ch->consumedFence)<ch->heldReady)){if(!handOverRing(ch,link,link.peerPid()))break;continue;}
                        ch->pending.clear(); // stale: the old ring is going away
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
            if (msg->type != fl::MsgType::FrameReady && msg->type!=fl::MsgType::FrameReadyMetadata) continue;
            flFrameMetadata metadata{};
            const auto* fr=fl::decodeFrame(*msg,(ch->flags&FL_REQUIRE_FRAME_METADATA)!=0,metadata);
            if(!fr || fr->slot>=ch->images.size()){break;}
            std::lock_guard<std::mutex> hold(ch->pendingMutex);
            // Oldest-first, bounded by flSetReceiveQueue (default 1: latest-frame-
            // wins). A full queue replaces its oldest frame; a slow consumer must
            // never stall the producer.
            if(fltrace::enabled()) {
                fltrace::Event e{"ipc.frame_received",fltrace::clockNs(),0,fltrace::id(ch->name.c_str()),
                    ch->sequence+1,fr->readyValue,fr->slot,fr->ptsNs,ch->pending.size()==ch->pending.depth()?1:0};
                fltrace::record(e); // result=1: unconsumed pending frame replaced
            }
            fl::PendingFrame frame;
            frame.slot=fr->slot;frame.ready=fr->readyValue;frame.pts=fr->ptsNs;frame.metadata=metadata;
            frame.sequence=++ch->sequence;
            if(ch->flags & FL_TRACK_ARRIVAL)frame.arrival=fl::arrivalNowNs();
            ch->pending.push(frame);
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
        case FL_DEVICE_LOST: return "GPU device lost or reset";
    }
    return "unknown";
}

static flResult createChannel(const char* name, uint32_t width, uint32_t height,
    flFormat format, uint32_t poolSize, uint32_t flags,
    const uint8_t adapterLUID[8], void* const* externalHandles, flChannel** out,
    flD3D11DeviceOwner* owner=nullptr) {
    fltrace::Scope trace("flCreateChannelOnAdapter",fltrace::id(name));
    if(trace.active){trace.event.sequence=((uint64_t)width<<32)|height;trace.event.fence=(uint64_t)format;trace.event.slot=poolSize;trace.event.pts=flags;}

    if(out)*out=nullptr;
    if (!name || !out || !fl::validGeometry(width,height,format)) return trace.result(FL_INVALID);
    *out = nullptr;
    if (format != FL_FORMAT_BGRA8 && format != FL_FORMAT_RGBA8 && format != FL_FORMAT_RGB10A2 && format != FL_FORMAT_NV12) return trace.result(FL_FORMAT_UNSUPPORTED);
    if (poolSize == 0 || poolSize > kMaxPool) return trace.result(FL_INVALID);

    auto* ch = new flChannel();
    ch->isConsumer = true;
    ch->flags = flags;
    ch->name = name;
    const uint8_t zeroLuid[8]{};
    const bool explicitAdapter = adapterLUID && memcmp(adapterLUID, zeroLuid, 8) != 0;
    bool adapterMissing=false;
    if(owner) {
        auto result=attachDevice(ch,owner,adapterLUID,explicitAdapter);
        if(result!=FL_OK){flClose(ch);return trace.result(result);}
    }else ch->device = createDevice(adapterLUID, explicitAdapter, &adapterMissing);
    if (adapterMissing) { flClose(ch); return trace.result(FL_ADAPTER_MISMATCH); }
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

    ch->externalPool=externalHandles!=nullptr;
    flResult allocation=externalHandles?adoptRing(ch,externalHandles,poolSize):
        (allocateRing(ch,width,height,poolSize)?FL_OK:FL_INTERNAL);
    if(allocation!=FL_OK){flClose(ch);return trace.result(allocation);}
    if (!resetSync(ch)) {
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

FL_API flResult flCreateD3D11DeviceOwner(const uint8_t luid[8], flD3D11DeviceOwner** out) {
    if(!out)return FL_INVALID;
    *out=nullptr;
    auto owner=std::make_unique<flD3D11DeviceOwner>();
    const uint8_t zero[8]{}; bool missing=false;
    owner->device=createDevice(luid,luid && memcmp(luid,zero,8),&missing);
    if(missing)return FL_ADAPTER_MISMATCH;
    if(!owner->device)return FL_INTERNAL;
    ComPtr<ID3D11Device5> dev5;
    ComPtr<ID3D11DeviceContext> context;
    owner->device->GetImmediateContext(&context);
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC desc{};
    if(FAILED(owner->device.As(&dev5)) || FAILED(context.As(&owner->context)) ||
       FAILED(owner->device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
       FAILED(adapter->GetDesc(&desc)))return FL_FORMAT_UNSUPPORTED;
    memcpy(owner->luid,&desc.AdapterLuid,8);
    *out=owner.release();return FL_OK;
}
FL_API void flRetainD3D11DeviceOwner(flD3D11DeviceOwner* owner) {
    if(owner)++owner->references;
}
FL_API void flReleaseD3D11DeviceOwner(flD3D11DeviceOwner* owner) {
    if(owner && --owner->references==0)delete owner;
}
FL_API ID3D11Device* flD3D11DeviceOwnerDevice(flD3D11DeviceOwner* owner) {
    return owner?owner->device.Get():nullptr;
}
FL_API flResult flD3D11DeviceOwnerAdapter(flD3D11DeviceOwner* owner,uint8_t out[8]) {
    if(!owner || !out)return FL_INVALID;
    memcpy(out,owner->luid,8);return FL_OK;
}
FL_API flResult flCreateChannelWithD3D11DeviceOwner(flD3D11DeviceOwner* owner,
    const char* name,uint32_t width,uint32_t height,flFormat format,uint32_t poolSize,
    uint32_t flags,flChannel** out) {
    if(out)*out=nullptr;
    if(!owner)return FL_INVALID;
    return createChannel(name,width,height,format,poolSize,flags,nullptr,nullptr,out,owner);
}
FL_API flResult flCreateChannelFromD3D11HandlesWithDeviceOwner(flD3D11DeviceOwner* owner,
    const char* name,uint32_t width,uint32_t height,flFormat format,uint32_t poolSize,
    uint32_t flags,void*const* handles,flChannel** out) {
    if(out)*out=nullptr;
    if(!owner || !handles || (flags & ~(FL_MAP_CPU|FL_GPU_SYNC|FL_DEFER_RELEASE|FL_GPU_RENDER|FL_TRACK_ARRIVAL|FL_REQUIRE_FRAME_METADATA|FL_VULKAN_IMPORT)))return FL_INVALID;
    return createChannel(name,width,height,format,poolSize,flags,nullptr,handles,out,owner);
}

FL_API flResult flCreateChannelOnAdapter(const char* name, uint32_t width, uint32_t height,
    flFormat format, uint32_t poolSize, uint32_t flags, const uint8_t adapterLUID[8], flChannel** out) {
    return createChannel(name,width,height,format,poolSize,flags,adapterLUID,nullptr,out);
}

FL_API flResult flCreateChannelFromD3D11Handles(const char* name, uint32_t width,
    uint32_t height, flFormat format, uint32_t poolSize, uint32_t flags,
    const uint8_t adapterLUID[8], void* const* handles, flChannel** out) {
    if(out)*out=nullptr;
    if(!handles || (flags & ~(FL_MAP_CPU|FL_GPU_SYNC|FL_DEFER_RELEASE|FL_GPU_RENDER|FL_TRACK_ARRIVAL|FL_REQUIRE_FRAME_METADATA|FL_VULKAN_IMPORT)))return FL_INVALID;
    return createChannel(name,width,height,format,poolSize,flags,adapterLUID,handles,out);
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

static flResult importRing(flChannel*, const fl::RingDescMsg*);

static flResult openProducer(const char* name, flChannel** out, SetupDeadline* setup, flD3D11DeviceOwner* owner=nullptr) {
    fltrace::Scope trace("flOpenProducer",fltrace::id(name));

    if (!name || !out) return trace.result(FL_INVALID);
    *out=nullptr;
    if(setup && setup->check()!=FL_OK)return setup->check();
    auto* ch = new flChannel();
    ch->isConsumer = false;
    ch->name = name;
    // Short timeout and no retry: a producer attaches to a channel that exists
    // or it does not. It never creates one.
    bool busy = false;
    // Short timeout: a producer attaches to a channel or it does not. Walking a
    // set of slots must not take seconds per slot.
    uint32_t connectTimeout=250;
    if(setup) {
        if(setup->check()!=FL_OK){delete ch;return setup->check();}
        const ULONGLONG now=GetTickCount64();
        const ULONGLONG remaining=now<setup->deadline?setup->deadline-now:0;
        connectTimeout=(uint32_t)(remaining<250?remaining:250);
    }
    ch->link = fl::Channel::connect(name, connectTimeout, &busy);
    if (!ch->link.connected()) {
        delete ch;
        return trace.result(busy ? FL_BUSY : FL_NOT_FOUND);
    }

    // The consumer speaks first, so a version mismatch is caught before it
    // duplicates any handle to us.
    auto helloMsg = ch->link.recv();
    const auto* srv = helloMsg && helloMsg->type == fl::MsgType::Hello && helloMsg->payload.size()==sizeof(fl::HelloMsg)
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
    const uint32_t features=fl::localFeatures(0)|(setup?(kPullRingFeature|(kPullRingFeature<<16)):0);
    if(!fl::compatibleFeatures(features,srv->_pad)){delete ch;return trace.result(FL_VERSION_MISMATCH);}
    ch->peerFeatures=srv->_pad;
    fl::HelloMsg mine{};
    mine.version = FL_VERSION;
    mine._pad=features;
    mine.pid = GetCurrentProcessId();
    if (!ch->link.send(fl::MsgType::Hello, mine)) {
        delete ch;
        return trace.result(FL_INTERNAL);
    }
    // Open on the consumer's adapter. Getting this wrong is the cross-adapter
    // failure the API promises to refuse rather than crash on.
    bool adapterMissing=false;
    if(owner) {
        if(!srv->luidValid){flClose(ch);return FL_ADAPTER_MISMATCH;}
        auto result=attachDevice(ch,owner,srv->adapterLUID,true);
        if(result!=FL_OK){flClose(ch);return trace.result(result);}
    }else ch->device = createDevice(srv->adapterLUID, srv->luidValid != 0, &adapterMissing);
    if (adapterMissing) { flClose(ch); return trace.result(FL_ADAPTER_MISMATCH); }
    if (!ch->device || !finishDeviceSetup(ch)) {
        flClose(ch);
        return trace.result(FL_INTERNAL);
    }
    memcpy(ch->info.adapterLUID, srv->adapterLUID, 8);

    if(setup && setup->check()!=FL_OK){flClose(ch);return setup->check();}
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

    ch->peerFeatures=setup?(srv->_pad|(kPullRingFeature<<16)):srv->_pad;
    auto imported=importRing(ch,pd);
    if(imported!=FL_OK){flClose(ch);return trace.result(imported);}
    ch->info.version=FL_VERSION;
    *out = ch;
    return trace.result(FL_OK);
}

FL_API flResult flOpenProducer(const char* name, flChannel** out) {
    return openProducer(name,out,nullptr);
}

static flResult openProducerBounded(const char* name, uint32_t timeoutMs,
                                         void* cancelEvent, flChannel** out, flD3D11DeviceOwner* owner) {
    if(out)*out=nullptr;
    if(!name || !out || !timeoutMs || timeoutMs>60000)return FL_INVALID;
    SetupDeadline setup(timeoutMs,(HANDLE)cancelEvent);
    flResult result=openProducer(name,out,&setup,owner);
    // Establish one completion boundary: the watcher cannot observe a later
    // cancellation after this final check but before success publication.
    setup.finish();
    flResult stopped=setup.check();
    if(stopped!=FL_OK) {
        if(*out){(*out)->link=fl::Channel();flClose(*out);*out=nullptr;}
        return stopped;
    }
    return result;
}

FL_API flResult flOpenProducerD3D11Bounded(const char* name, uint32_t timeoutMs,
                                         void* cancelEvent, flChannel** out) {
    return openProducerBounded(name,timeoutMs,cancelEvent,out,nullptr);
}
FL_API flResult flOpenProducerD3D11BoundedWithDeviceOwner(flD3D11DeviceOwner* owner,
    const char* name, uint32_t timeoutMs, void* cancelEvent, flChannel** out) {
    if(out)*out=nullptr;
    if(!owner)return FL_INVALID;
    return openProducerBounded(name,timeoutMs,cancelEvent,out,owner);
}

// Producer: replace our view of the ring from a RingDesc. Used at attach and
// again whenever the consumer reallocates.
static flResult importRing(flChannel* ch, const fl::RingDescMsg* source) {
    fl::RingDescMsg local=*source;
    const bool pull=(ch->peerFeatures & (kPullRingFeature<<16))!=0;
    if(pull) { memset(local.texture,0,sizeof(local.texture)); local.readyFence=local.consumedFence=0; }
    const fl::RingDescMsg* pd=&local;
    // Descriptor ownership transfers on receipt, including every failure path.
    // Stage all imports before replacing the old ring and borrowed image identity.
    struct Incoming {
        const fl::RingDescMsg* pd;
        bool transferred=false;
        ~Incoming() {
            if(transferred)return;
            uint64_t handles[FL_MAX_POOL_SIZE+2]{};
            for(uint32_t i=0;i<pd->poolSize && i<FL_MAX_POOL_SIZE;++i)handles[i]=pd->texture[i];
            handles[FL_MAX_POOL_SIZE]=pd->readyFence;
            handles[FL_MAX_POOL_SIZE+1]=pd->consumedFence;
            for(size_t i=0;i<FL_MAX_POOL_SIZE+2;++i)if(handles[i]) {
                bool duplicate=false;
                for(size_t j=0;j<i;++j)if(handles[j]==handles[i])duplicate=true;
                if(!duplicate)CloseHandle((HANDLE)(uintptr_t)handles[i]);
            }
        }
    } incoming{pd};
    if(pull) {
        if(!source->poolSize || source->poolSize>FL_MAX_POOL_SIZE)return FL_INVALID;
        HANDLE process=OpenProcess(PROCESS_DUP_HANDLE,FALSE,ch->link.peerPid());
        if(!process)return FL_ACCESS_DENIED;
        auto duplicate=[process](uint64_t value,uint64_t& dest) {
            HANDLE handle=nullptr;
            if(!value || !DuplicateHandle(process,(HANDLE)(uintptr_t)value,GetCurrentProcess(),&handle,
                                         0,FALSE,DUPLICATE_SAME_ACCESS))return false;
            dest=(uint64_t)(uintptr_t)handle;return true;
        };
        bool ok=true;
        for(uint32_t i=0;i<source->poolSize && ok;++i)ok=duplicate(source->texture[i],local.texture[i]);
        ok=ok && duplicate(source->readyFence,local.readyFence) && duplicate(source->consumedFence,local.consumedFence);
        CloseHandle(process);
        if(!ok)return FL_ACCESS_DENIED;
    }
    if(!pd->poolSize || pd->poolSize>FL_MAX_POOL_SIZE ||
       !fl::validGeometry(pd->width,pd->height,(flFormat)pd->format))return FL_INVALID;
    ComPtr<ID3D11Device1> dev1;
    ComPtr<ID3D11Device5> dev5;
    if(FAILED(ch->device.As(&dev1)) || FAILED(ch->device.As(&dev5)))return FL_INTERNAL;
    std::vector<std::unique_ptr<flImage>> staged;
    for(uint32_t i=0;i<pd->poolSize;++i) {
        auto img=std::make_unique<flImage>();
        img->producer=true;img->shared=(HANDLE)(uintptr_t)pd->texture[i];
        if(FAILED(dev1->OpenSharedResource1(img->shared,IID_PPV_ARGS(&img->tex))))return FL_ADAPTER_MISMATCH;
        D3D11_TEXTURE2D_DESC desc{};img->tex->GetDesc(&desc);
        const DXGI_FORMAT expected=pd->format==FL_FORMAT_NV12?DXGI_FORMAT_NV12:
            pd->format==FL_FORMAT_RGBA8?DXGI_FORMAT_R8G8B8A8_UNORM:
            pd->format==FL_FORMAT_RGB10A2?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
        if(desc.Format!=expected || desc.Width!=pd->width || desc.Height!=pd->height || desc.ArraySize!=1 ||
           desc.MipLevels!=1 || desc.SampleDesc.Count!=1)return FL_INVALID;
        staged.push_back(std::move(img));
    }
    ComPtr<ID3D11Fence> ready,consumed;
    if(FAILED(dev5->OpenSharedFence((HANDLE)(uintptr_t)pd->readyFence,IID_PPV_ARGS(&ready))) ||
       FAILED(dev5->OpenSharedFence((HANDLE)(uintptr_t)pd->consumedFence,IID_PPV_ARGS(&consumed))))return FL_INTERNAL;
    // Allocate bookkeeping before committing, so old imports stay intact on error.
    std::vector<flImage*> images;images.reserve(staged.size());
    std::vector<uint64_t> slots(staged.size(),0);
    for(auto& img:staged)images.push_back(img.get());
    for(auto* img:ch->images){if(img->shared)CloseHandle(img->shared);delete img;}
    if(ch->readyShared)CloseHandle(ch->readyShared);
    if(ch->consumedShared)CloseHandle(ch->consumedShared);
    ch->images=std::move(images);ch->slotReadyValue=std::move(slots);
    for(auto& img:staged)img.release();
    ch->readyShared=(HANDLE)(uintptr_t)pd->readyFence;
    ch->consumedShared=(HANDLE)(uintptr_t)pd->consumedFence;
    ch->readyFence=ready;ch->consumedFence=consumed;
    incoming.transferred=true;
    ch->ctx->Flush();
    ch->info.generation=pd->generation;ch->info.width=pd->width;ch->info.height=pd->height;
    ch->info.format=(flFormat)pd->format;ch->info.poolSize=pd->poolSize;
    return FL_OK;
}

FL_API flResult flRequestGeometry(flChannel* ch, uint32_t width, uint32_t height,
                                  flFormat format) {
    fltrace::Scope trace("flRequestGeometry", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !fl::validGeometry(width,height,format)) return trace.result(FL_INVALID);
    if(format != ch->info.format) return trace.result(FL_FORMAT_UNSUPPORTED);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    if(!ch->bufferReferences.empty())return trace.result(FL_BUSY);
    for(uint64_t value:ch->slotReadyValue)
        if(value==UINT64_MAX)return trace.result(FL_BUSY);
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
    if(pd->generation==ch->info.generation) {
        if(ch->peerFeatures & (kPullRingFeature<<16))return trace.result(FL_OK);
        // Refusal/no-op: these are newly duplicated handles to the SAME storage.
        // Keep slot reservations, borrowed image identity and fence counters.
        for(uint32_t i=0;i<pd->poolSize;i++)
            if(pd->texture[i])CloseHandle((HANDLE)(uintptr_t)pd->texture[i]);
        if(pd->readyFence)CloseHandle((HANDLE)(uintptr_t)pd->readyFence);
        if(pd->consumedFence)CloseHandle((HANDLE)(uintptr_t)pd->consumedFence);
        return trace.result(FL_OK);
    }
    ch->readyValue = 0; // fences are fresh with the new ring
    return trace.result(importRing(ch, pd));
}

FL_API flResult flAcquireBuffer(flChannel* ch, flBuffer* out, uint32_t timeoutMs) {
    fltrace::Scope trace("flAcquireBuffer", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !out) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    const DWORD deadline = GetTickCount() + timeoutMs;
    for (;;) {
        // Our own removed device is device loss even while the fences still
        // read normally (measured: after an adapter removal the producer only
        // timed out, its consumer having stopped releasing). Decision 13.
        if (ch->deviceLost.load() || (ch->device && deviceRemoved(ch->device))) {
            ch->deviceLost=true;return trace.result(FL_DEVICE_LOST);
        }
        const uint64_t consumed = completedValue(ch->consumedFence);
        if(consumed==UINT64_MAX){ // a device holding the fence is gone; not all slots free
            const flResult lost=fenceLost(ch->device);if(lost==FL_DEVICE_LOST)ch->deviceLost=true;
            return trace.result(lost);
        }
        bool localBlocked = false;
        for (size_t i = 0; i < ch->images.size(); ++i) {
            // Free once the consumer has promised never to read this slot's
            // previous contents again.
            if (ch->slotReadyValue[i] <= consumed) {
                if (ch->bufferReferences.busy((uint32_t)i)) {
                    localBlocked = true;
                    continue;
                }
                ch->slotReadyValue[i] = UINT64_MAX;ch->slotMetadata[i]={}; // reserve until submit
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
        if (now >= deadline) {
            if (trace.active) {
                static const bool ringTrace=[] {
                    const char* flag=std::getenv("FRAMELINK_TRACE_RING");
                    return flag && flag[0] && std::strcmp(flag,"0")!=0;
                }();
                if (ringTrace) {
                    const auto at=fltrace::clockNs();
                    fltrace::record({"ring.acquire_timeout",at,0,trace.event.object,
                        ch->readyValue,consumed,ch->images.size(),-1,FL_TIMEOUT});
                    for(size_t i=0;i<ch->slotReadyValue.size();++i)
                        fltrace::record({"ring.slot_state",at,0,trace.event.object,
                            ch->slotReadyValue[i],consumed,i,-1,FL_TIMEOUT});
                }
            }
            return trace.result(FL_TIMEOUT);
        }
        // Preserve what prevented reuse during this scan. Re-reading empty()
        // here can miss a finalizer release between the scan and wait, then
        // sleep on an unrelated outstanding remote fence for the full timeout.
        // Poll local finalizers without accumulating repeated GPU event requests.
        if (localBlocked) { Sleep(1); continue; }
        uint64_t oldest = UINT64_MAX;
        for (uint64_t v : ch->slotReadyValue)
            if (v > consumed && v < oldest) oldest = v;
        if (oldest == UINT64_MAX) { Sleep(1); continue; }
        ch->consumedFence->SetEventOnCompletion(oldest, ch->waitEvent);
        fltrace::call("wait.consumed_fence", [&]{return WaitForSingleObject(ch->waitEvent, deadline-now);});
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

FL_API flResult flCancelBuffer(flChannel* ch, const flBuffer* buf) {
    if(!ch || ch->isConsumer || !buf || buf->slot>=ch->images.size() ||
       buf->image!=ch->images[buf->slot] || ch->slotReadyValue[buf->slot]!=UINT64_MAX)
        return FL_INVALID;
    // After begin, GPU work may already target the reserved signal value.
    if(ch->submittingValue && ch->submittingSlot==buf->slot)return FL_BUSY;
    // Caller promises no outstanding CPU/GPU access. No implicit GPU wait.
    flImageUnmap(buf->image);
    ch->slotReadyValue[buf->slot]=0;
    return FL_OK;
}

FL_API flResult flBeginSubmit(flChannel* ch, const flBuffer* buf, uint64_t* signalValue) {
    fltrace::Scope trace("flBeginSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));

    if (!ch || ch->isConsumer || !buf || !signalValue ||
        buf->slot >= ch->images.size() || buf->image != ch->images[buf->slot] ||
        ch->slotReadyValue[buf->slot] != UINT64_MAX) return trace.result(FL_INVALID);
    if (ch->deviceLost.load() || (ch->device && deviceRemoved(ch->device))) {
        ch->deviceLost=true;return trace.result(FL_DEVICE_LOST);
    }
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    if (ch->submittingValue) return trace.result(FL_BUSY);
    if((ch->peerFeatures&(fl::FrameMetadataFeature<<16))&&!ch->slotMetadata[buf->slot].version)return trace.result(FL_FORMAT_UNSUPPORTED);
    // UINT64_MAX is the reserved-slot sentinel, never a publishable fence value.
    if (ch->readyValue >= UINT64_MAX-1) return trace.result(FL_INVALID);
    ch->submittingSlot = buf->slot;
    ch->submittingValue = ++ch->readyValue;
    *signalValue = ch->submittingValue;
    if(trace.active) trace.event.fence=*signalValue;
    return trace.result(FL_OK);
}

FL_API flResult flEndSubmit(flChannel* ch, const flBuffer* buf, uint64_t signalValue,
                            int64_t ptsNs) {
    fltrace::Scope trace("flEndSubmit", fltrace::id(ch ? ch->name.c_str() : nullptr));
    if(trace.active) { trace.event.pts=ptsNs; trace.event.slot=buf?buf->slot:0; }
    if(trace.active) trace.event.fence=signalValue;

    if (!ch || ch->isConsumer || !buf || buf->slot >= ch->images.size() ||
        buf->image != ch->images[buf->slot] || ch->slotReadyValue[buf->slot] != UINT64_MAX ||
        !signalValue || signalValue != ch->submittingValue ||
        buf->slot != ch->submittingSlot) return trace.result(FL_INVALID);
    if (!ch->link.connected()) return trace.result(FL_DISCONNECTED);
    ch->slotReadyValue[buf->slot] = signalValue;
    ch->submittingValue = 0; ch->submittingSlot = UINT32_MAX;

    fl::FrameReadyMsg fr{};
    fr.slot = buf->slot;
    fr.readyValue = signalValue;
    fr.ptsNs = ptsNs;
    return trace.result(fl::sendFrame(ch->link,fr,ch->slotMetadata[buf->slot]) ? FL_OK : FL_DISCONNECTED);
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
            if (ch->deviceLost.load()) return trace.result(FL_DEVICE_LOST);
            // Our removed device is device loss, never readiness (decision 13):
            // nothing is delivered. The fence and device exist only once a ring
            // is attached.
            const uint64_t readyCompleted=ch->readyFence?completedValue(ch->readyFence):0;
            if (ch->device && deviceRemoved(ch->device)) {
                ch->deviceLost=true;
                return trace.result(FL_DEVICE_LOST);
            }
            // UINT64_MAX with our device alive means the producer's process
            // or device is gone; that path is unchanged here (decision 13
            // changes device loss only).
            if (!ch->pending.empty() && !ch->frameHeld &&
                ((ch->flags & FL_GPU_SYNC) || readyCompleted >= ch->pending.front().ready)) {
                const fl::PendingFrame f=ch->pending.pop();
                ch->frameHeld = true;ch->heldArrival=f.arrival;
                ch->heldReady = f.ready;
                ch->heldSlot = f.slot;ch->heldMetadata=f.metadata;
                slot = f.slot;
                ready = f.ready;
                pts = f.pts;
                seq = f.sequence;
                got = true;
            }
        }
        if (got) {
            ++g_framesAcquired; // test fault injection's frame count only
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
        if(!ch->readers.empty())return FL_BUSY;
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


static void releaseLeaseReadyFence(flChannel*) {}
static flResult retireReadLeases(flChannel* ch,uint64_t value) {
    if(FAILED(ch->ctx->Signal(ch->consumedFence.Get(),value)))return FL_INTERNAL;
    ch->ctx->Flush();ch->consumedValue=value;return FL_OK;
}
static flResult pollLeaseCompletions(flChannel* ch) {
    for(auto& pending:ch->leaseCompletions) if(pending.token) {
        uint64_t value=completedValue(pending.fence);
        if(value==UINT64_MAX){ // Device loss is not completion.
            const flResult lost=fenceLost(ch->device);if(lost==FL_DEVICE_LOST)ch->deviceLost=true;
            return lost;
        }
        if(value>=pending.value) {
            auto r=ch->readers.finishDeferred(pending.token);if(r!=FL_OK)return r;
            pending.fence.Reset();pending.token=0;pending.value=0;
        }
    }
    return FL_OK;
}
FL_API flResult flCompleteReadLeaseD3D11Fence(flChannel* ch,uint64_t token,void* handle,uint64_t value) {
    if(!ch || !ch->isConsumer || !handle || value==UINT64_MAX)return FL_INVALID;
    std::lock_guard<std::mutex> lock(ch->pendingMutex);
    flFrame frame{};uint64_t generation=0;
    if(ch->readers.get(token,frame,generation)!=FL_OK)return FL_INVALID;
    for(auto& pending:ch->leaseCompletions) if(!pending.token) {
        ComPtr<ID3D11Device5> device;
        if(FAILED(ch->device.As(&device)))return FL_FORMAT_UNSUPPORTED;
        ComPtr<ID3D11Fence> fence;
        if(FAILED(device->OpenSharedFence(handle,IID_PPV_ARGS(&fence))))return FL_INVALID;
        auto r=ch->readers.defer(token);if(r!=FL_OK)return r;
        pending.fence=fence;pending.value=value;pending.token=token;return FL_OK;
    }
    return FL_BUSY;
}

static bool metadataReservation(flChannel* ch,const flBuffer* buffer) {
    return buffer && buffer->slot<ch->images.size() && buffer->image==ch->images[buffer->slot] && ch->slotReadyValue[buffer->slot]==UINT64_MAX;
}
#include "fl_read_api.inc"

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
    if(ch->attachment) {
        // No hidden GPU wait or invented lease completion. An unsafe retirement
        // permanently rejects reuse; fresh generation allocation is not a waiver.
        auto* owner=ch->attachment->owner;
        const uint64_t required=ch->isConsumer?ch->heldReady:ch->readyValue;
        const bool incomplete=ch->frameHeld || !ch->readers.empty() || !ch->pending.empty() ||
            ch->submittingSlot!=UINT32_MAX ||
            (ch->isConsumer && ch->consumedFence && completedValue(ch->consumedFence)<required) ||
            deviceRemoved(owner->device);
        { std::lock_guard<std::mutex> lock(owner->mutex); owner->poisoned|=incomplete;
          // A remote reader may legitimately outlive producer close. Pin its
          // actual consumed fence, not storage for reuse; the next attachment
          // stays BUSY until real completion, never treating close as drain.
          if(!ch->isConsumer && ch->consumedFence && completedValue(ch->consumedFence)<required) {
              owner->retiringFence=ch->consumedFence;owner->retiringValue=required;
          }
        }
        // Clear bindings from this retired graph; does not wait for GPU completion.
        if(ch->ctx){ch->ctx->ClearState();ch->ctx->Flush();}
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

FL_API flResult flSnapshotD3D11ConsumerPool(flChannel* ch, flD3D11PoolSnapshot** out) {
    if(out)*out=nullptr;
    if(!ch || !ch->isConsumer || !out)return FL_INVALID;
    std::lock_guard<std::recursive_mutex> poolLock(ch->poolMutex);
    if(ch->stop.load() || !ch->info.generation || ch->images.size()!=ch->info.poolSize)return FL_DISCONNECTED;
    if(deviceRemoved(ch->device))return FL_DEVICE_LOST;
    auto snapshot=std::make_unique<flD3D11PoolSnapshot>();
    snapshot->info=ch->info;
    snapshot->handles.resize(ch->images.size(),nullptr);
    for(size_t i=0;i<ch->images.size();++i) {
        if(!DuplicateHandle(GetCurrentProcess(),ch->images[i]->shared,GetCurrentProcess(),
                            &snapshot->handles[i],0,FALSE,DUPLICATE_SAME_ACCESS))return FL_INTERNAL;
    }
    snapshot->attachment=ch->attachment;
    snapshot->pins=ch->poolPins;
    ++*snapshot->pins;
    *out=snapshot.release();
    return FL_OK;
}

FL_API flResult flQueryD3D11PoolSnapshot(const flD3D11PoolSnapshot* snapshot, flChannelInfo* out) {
    if(!snapshot || !out)return FL_INVALID;
    *out=snapshot->info;
    return FL_OK;
}

FL_API void* flD3D11PoolSnapshotHandle(const flD3D11PoolSnapshot* snapshot, uint32_t slot) {
    if(!snapshot || slot>=snapshot->handles.size())return nullptr;
    return snapshot->handles[slot];
}

FL_API void flReleaseD3D11PoolSnapshot(flD3D11PoolSnapshot* snapshot) { delete snapshot; }

// ---- D3D11 interop -----------------------------------------------------------

FL_API flResult flImageMemoryInfo(flChannel* ch, flImage* img, flMemoryResourceInfo* out, size_t size) {
    if(!ch || !img || !out || size!=sizeof(*out)) return FL_INVALID;
    if(out->version!=FL_MEMORY_INFO_VERSION) return FL_VERSION_MISMATCH;
    bool member=false; for(auto* image:ch->images) if(image==img) member=true;
    if(!member || !img->tex) return FL_INVALID;
    D3D11_TEXTURE2D_DESC d{}; img->tex->GetDesc(&d);
    flMemoryResourceInfo r{}; r.version=FL_MEMORY_INFO_VERSION;
    r.resourceType=FL_MEMORY_D3D11; r.logicalFormat=ch->info.format;
    r.width=d.Width; r.height=d.Height; r.nativeFormat=d.Format;
    r.nativeUsage=d.BindFlags; r.generation=ch->info.generation;
    r.knownFields=FL_MEMORY_INFO_NATIVE_USAGE | FL_MEMORY_INFO_DIRECT_USAGE | FL_MEMORY_INFO_MAP_OPERATIONS;
    if(d.BindFlags & D3D11_BIND_SHADER_RESOURCE) r.directUsage|=FL_MEMORY_GPU_SAMPLE;
    if(d.BindFlags & D3D11_BIND_RENDER_TARGET) r.directUsage|=FL_MEMORY_GPU_RENDER;
    if(d.BindFlags & D3D11_BIND_DECODER) r.directUsage|=FL_MEMORY_DECODER_OUTPUT;
    r.mapOperations=FL_MEMORY_COPY | FL_MEMORY_READBACK;
    if(img->producer) r.mapOperations|=FL_MEMORY_UPLOAD;
    if(img->mapped.pData) {
        r.knownFields|=FL_MEMORY_INFO_MAPPED_USAGE;
        r.mappedUsage=FL_MEMORY_CPU_READ | (img->producer ? FL_MEMORY_CPU_WRITE : 0);
    }
    ComPtr<ID3D11Device> device; img->tex->GetDevice(&device);
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC a{};
    if(SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&a))) {
        r.knownFields|=FL_MEMORY_INFO_DEVICE; r.deviceIdKind=FL_MEMORY_DEVICE_DXGI_LUID;
        std::memcpy(r.deviceId,&a.AdapterLuid,sizeof(a.AdapterLuid));
    }
    r.syncTransport=FL_MEMORY_SYNC_CPU_WAIT | FL_MEMORY_SYNC_TIMELINE;
    r.handleTransport=FL_MEMORY_HANDLE_NT;
    r.interopCapabilities=FL_MEMORY_INTEROP_RING_IMPORT | FL_MEMORY_INTEROP_BORROW_NATIVE;
    if(ch->externalPool)r.interopCapabilities|=FL_MEMORY_INTEROP_EXTERNAL_POOL;
    if(fltrace::enabled()) {
        fltrace::Event e{};e.op="image.memory_resource";e.start=fltrace::clockNs();
        e.object=(uint64_t)(uintptr_t)img;e.details=fltrace::Memory|fltrace::Generation;
        e.knownFields=r.knownFields;e.generation=r.generation;e.resource=r.resourceType;e.usage=r.directUsage;e.operations=r.mapOperations;
        if(r.knownFields&FL_MEMORY_INFO_DEVICE) {e.details|=fltrace::Device;e.deviceKind=r.deviceIdKind;
            for(unsigned i=0;i<8;i++){e.deviceLo|=(uint64_t)r.deviceId[i]<<(8*i);e.deviceHi|=(uint64_t)r.deviceId[i+8]<<(8*i);}}
        fltrace::record(e);
    }
    *out=r; return FL_OK;
}

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
        fltrace::payload("image.gpu_readback_issued",(uint64_t)(uintptr_t)img,
            (uint64_t)desc.Width*desc.Height*(desc.Format==DXGI_FORMAT_NV12?3:8)/2,FL_MEMORY_READBACK);
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
    if (img->producer) {
        img->mapContext->CopyResource(img->tex.Get(), img->staging.Get());
        if(fltrace::enabled()) {
            D3D11_TEXTURE2D_DESC desc{};img->tex->GetDesc(&desc);
            fltrace::payload("image.gpu_upload_issued",(uint64_t)(uintptr_t)img,
                (uint64_t)desc.Width*desc.Height*(desc.Format==DXGI_FORMAT_NV12?3:8)/2,FL_MEMORY_UPLOAD);
        }
    }
}

FL_API ID3D11Texture2D* flImageD3D11(flImage* img) { return img ? img->tex.Get() : nullptr; }
FL_API ID3D11Device* flChannelD3D11Device(flChannel* ch) {
    return ch ? ch->device.Get() : nullptr;
}
FL_API void* flImageSharedHandle(flImage* img) { return img ? img->shared : nullptr; }

} // extern "C"
