// framelink_vulkan.h on real hardware: zero-copy import, Vulkan producer and
// consumer, GPU readiness/completion ordering, adapter identity and ring
// reallocation. One binary for Windows, Linux and Android.
//
//   --formats     every format x {vk->cpu, vk->vk GPU_SYNC, vk->vk DEFER_RELEASE,
//                 cpu->vk}. Pixels are checked byte for byte on the other side.
//   --ordering    host-gated GPU work proves the consumer waits for readiness
//                 and the producer waits for the consumer's GPU completion.
//   --adapter     flVkCheckAdapter accepts the ring's GPU and rejects another.
//   --generation  a geometry change invalidates imports; re-import works.
//
// Exit 77 (skip) when there is no Vulkan loader or no device with the required
// extensions. FL_VK_NEGATIVE=skip-ready-wait|early-consumed-signal breaks the
// ordering test on purpose, and =no-import-flag drops FL_VULKAN_IMPORT from
// --formats; each must then fail (the last only where the default allocation
// is not importable, e.g. Linux/NVIDIA).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "framelink_vulkan.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

// A failure can leave GPU work blocked on a host event; normal exit would then
// wait in driver teardown forever. Release every gate and leave immediately.
std::vector<std::function<void()>> g_onFail;
[[noreturn]] void failNow() {
    for (auto& f : g_onFail) f();
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(1);
}

} // namespace

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failNow(); } } while (0)

namespace {

#define FL_TEST_VK_FUNCS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) \
    X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceFeatures2) X(EnumerateDeviceExtensionProperties) \
    X(GetPhysicalDeviceMemoryProperties) X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) \
    X(DeviceWaitIdle) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer) X(CmdPipelineBarrier) \
    X(CmdClearColorImage) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) X(CmdWaitEvents) \
    X(QueueSubmit) X(CreateFence) X(DestroyFence) X(WaitForFences) X(GetFenceStatus) \
    X(ResetFences) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateEvent) X(DestroyEvent) X(SetEvent) X(ResetEvent) X(CreateSemaphore) X(DestroySemaphore)

// One VkDevice per side. Producer and consumer are separate processes in real
// use; separate devices keep the test honest - nothing orders their work except
// FrameLink's fences (one queue would serialize them and hide a missing wait).
struct Dev {
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    flVkContext ctx{};
};

struct Vk {
    void* library = nullptr;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr; // global: queried with a NULL instance
#define FL_DECL(name) PFN_vk##name name = nullptr;
    FL_TEST_VK_FUNCS(FL_DECL)
#undef FL_DECL
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    bool ycbcr = false;
    std::string name;
    Dev prod, cons;
};

void* openLoader() {
#if defined(_WIN32)
    return (void*)LoadLibraryA("vulkan-1.dll");
#elif defined(__ANDROID__)
    return dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#else
    void* l = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    return l ? l : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

PFN_vkGetInstanceProcAddr loaderEntry(void* library) {
#if defined(_WIN32)
    return (PFN_vkGetInstanceProcAddr)GetProcAddress((HMODULE)library, "vkGetInstanceProcAddr");
#else
    return (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
#endif
}

[[noreturn]] void skip(const char* why) {
    std::printf("SKIP: %s\n", why);
    std::exit(77);
}

bool hasExtension(Vk& vk, VkPhysicalDevice phys, const char* name) {
    uint32_t n = 0;
    vk.EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vk.EnumerateDeviceExtensionProperties(phys, nullptr, &n, props.data());
    for (auto& p : props) if (!std::strcmp(p.extensionName, name)) return true;
    return false;
}

// Bring up a device on the GPU the probe ring lives on. flVkCheckAdapter picks
// it, so the selection itself exercises the adapter API.
void initVulkan(Vk& vk, const flChannelInfo& ring) {
    vk.library = openLoader();
    if (!vk.library) skip("no Vulkan loader");
    vk.gipa = loaderEntry(vk.library);
    if (!vk.gipa) skip("loader has no vkGetInstanceProcAddr");
    vk.CreateInstance = (PFN_vkCreateInstance)vk.gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "framelink vulkan test";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (!vk.CreateInstance || vk.CreateInstance(&ici, nullptr, &vk.instance) != VK_SUCCESS)
        skip("vkCreateInstance(1.2) failed");
#define FL_LOAD(name) vk.name = (PFN_vk##name)vk.gipa(vk.instance, "vk" #name); CHECK(vk.name);
    FL_TEST_VK_FUNCS(FL_LOAD)
#undef FL_LOAD
    uint32_t n = 0;
    vk.EnumeratePhysicalDevices(vk.instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devices(n);
    vk.EnumeratePhysicalDevices(vk.instance, &n, devices.data());
    uint32_t required = 0;
    const char* const* names = flVkRequiredDeviceExtensions(&required);
    std::string rejected;
    for (VkPhysicalDevice phys : devices) {
        VkPhysicalDeviceProperties props;
        vk.GetPhysicalDeviceProperties(phys, &props);
        if (props.apiVersion < VK_API_VERSION_1_2) { rejected += std::string(props.deviceName) + " (<1.2) "; continue; }
        bool all = true;
        for (uint32_t i = 0; i < required; ++i) all = all && hasExtension(vk, phys, names[i]);
        if (!all) { rejected += std::string(props.deviceName) + " (extensions) "; continue; }
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        f11.pNext = &f12;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &f11;
        vk.GetPhysicalDeviceFeatures2(phys, &features);
        if (!f12.timelineSemaphore) { rejected += std::string(props.deviceName) + " (timeline) "; continue; }
        uint32_t qn = 0;
        vk.GetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vk.GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.data());
        uint32_t family = UINT32_MAX;
        for (uint32_t q = 0; q < qn && family == UINT32_MAX; ++q)
            if (qf[q].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) family = q;
        if (family == UINT32_MAX) continue;
        std::vector<const char*> enable(names, names + required);
#if !defined(_WIN32) && !defined(__ANDROID__)
        if (hasExtension(vk, phys, "VK_EXT_physical_device_drm")) enable.push_back("VK_EXT_physical_device_drm");
#endif
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan12Features e12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        e12.timelineSemaphore = VK_TRUE;
        VkPhysicalDeviceVulkan11Features e11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        e11.samplerYcbcrConversion = f11.samplerYcbcrConversion;
        e11.pNext = &e12;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pNext = &e11;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = (uint32_t)enable.size();
        dci.ppEnabledExtensionNames = enable.data();
        VkDevice device = VK_NULL_HANDLE;
        if (vk.CreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) {
            rejected += std::string(props.deviceName) + " (vkCreateDevice) ";
            continue;
        }
        flVkContext ctx;
        CHECK(flVkInitContext(&ctx, vk.instance, phys, device, vk.gipa, nullptr) == FL_OK);
        flResult same = flVkCheckAdapter(&ctx, &ring);
        if (same != FL_OK) {
            rejected += std::string(props.deviceName) + " (" + flResultString(same) + ") ";
            vk.DestroyDevice(device, nullptr);
            continue;
        }
        VkDevice second = VK_NULL_HANDLE;
        CHECK(vk.CreateDevice(phys, &dci, nullptr, &second) == VK_SUCCESS);
        vk.phys = phys;
        vk.ycbcr = f11.samplerYcbcrConversion;
        vk.name = props.deviceName;
        vk.prod.device = device;
        vk.prod.ctx = ctx;
        vk.cons.device = second;
        CHECK(flVkInitContext(&vk.cons.ctx, vk.instance, phys, second, vk.gipa, nullptr) == FL_OK);
        vk.prod.family = vk.cons.family = family;
        break;
    }
    if (!vk.prod.device) {
        std::printf("devices rejected: %s\n", rejected.c_str());
        skip("no Vulkan device on the ring's adapter with the FrameLink interop extensions");
    }
    for (Dev* d : {&vk.prod, &vk.cons}) {
        vk.GetDeviceQueue(d->device, d->family, 0, &d->queue);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = d->family;
        CHECK(vk.CreateCommandPool(d->device, &pci, nullptr, &d->pool) == VK_SUCCESS);
    }
    std::printf("Vulkan: %s, separate producer/consumer devices (queue family %u, ycbcr %d)\n",
                vk.name.c_str(), vk.prod.family, (int)vk.ycbcr);
}

void destroyVulkan(Vk& vk) {
    for (Dev* d : {&vk.prod, &vk.cons}) {
        if (!d->device) continue;
        vk.DeviceWaitIdle(d->device);
        vk.DestroyCommandPool(d->device, d->pool, nullptr);
        vk.DestroyDevice(d->device, nullptr);
    }
    if (vk.instance) vk.DestroyInstance(vk.instance, nullptr);
}

struct HostBuffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint8_t* data = nullptr;
    VkDeviceSize size = 0;
};

HostBuffer makeHostBuffer(Vk& vk, Dev& dev, VkDeviceSize size) {
    HostBuffer h;
    h.device = dev.device;
    h.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    CHECK(vk.CreateBuffer(h.device, &bi, nullptr, &h.buffer) == VK_SUCCESS);
    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(h.device, h.buffer, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vk.GetPhysicalDeviceMemoryProperties(vk.phys, &mp);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t t = 0; t < mp.memoryTypeCount && type == UINT32_MAX; ++t)
        if ((req.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & want) == want) type = t;
    CHECK(type != UINT32_MAX);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    CHECK(vk.AllocateMemory(h.device, &ai, nullptr, &h.memory) == VK_SUCCESS);
    CHECK(vk.BindBufferMemory(h.device, h.buffer, h.memory, 0) == VK_SUCCESS);
    void* p = nullptr;
    CHECK(vk.MapMemory(h.device, h.memory, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS);
    h.data = (uint8_t*)p;
    return h;
}

void freeHostBuffer(Vk& vk, HostBuffer& h) {
    if (h.memory) vk.UnmapMemory(h.device, h.memory);
    if (h.buffer) vk.DestroyBuffer(h.device, h.buffer, nullptr);
    if (h.memory) vk.FreeMemory(h.device, h.memory, nullptr);
    h = HostBuffer{};
}

// ---- pixel model ------------------------------------------------------------

// Frame k's content. RGB formats: one solid color; NV12: solid Y and UV.
struct Pattern { uint32_t r, g, b, a; uint8_t y, u, v; };
Pattern pattern(uint32_t k) {
    return Pattern{(17u * k + 3u) & 0xffu, (0x40u + 29u * k) & 0xffu, (0x90u + 7u * k) & 0xffu,
                   0xffu, (uint8_t)(0x20 + 11 * k), (uint8_t)(0x60 + 5 * k), (uint8_t)(0xa0 - 3 * k)};
}

// Bytes of one pixel as a Vulkan copy (by VkFormat) or a FrameLink CPU map (by
// flFormat) lays them out.
void pixelBytes(VkFormat vf, const Pattern& p, uint8_t out[4]) {
    if (vf == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
        uint32_t r = p.r * 4u, g = p.g * 4u, b = p.b * 4u, w = r | (g << 10) | (b << 20) | (3u << 30);
        std::memcpy(out, &w, 4);
    } else if (vf == VK_FORMAT_B8G8R8A8_UNORM) {
        out[0] = (uint8_t)p.b; out[1] = (uint8_t)p.g; out[2] = (uint8_t)p.r; out[3] = (uint8_t)p.a;
    } else {
        out[0] = (uint8_t)p.r; out[1] = (uint8_t)p.g; out[2] = (uint8_t)p.b; out[3] = (uint8_t)p.a;
    }
}

VkClearColorValue clearValue(VkFormat vf, const Pattern& p) {
    VkClearColorValue c{};
    float scale = vf == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? 1023.0f : 255.0f;
    float mul = vf == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? 4.0f : 1.0f;
    c.float32[0] = p.r * mul / scale;
    c.float32[1] = p.g * mul / scale;
    c.float32[2] = p.b * mul / scale;
    c.float32[3] = vf == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? 1.0f : p.a / 255.0f;
    return c;
}

VkFormat cpuFormat(flFormat f) {
    // FrameLink CPU views are in the channel's declared byte order everywhere.
    switch (f) {
    case FL_FORMAT_BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
    case FL_FORMAT_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case FL_FORMAT_RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    default: return VK_FORMAT_UNDEFINED;
    }
}

const char* formatName(flFormat f) {
    switch (f) {
    case FL_FORMAT_BGRA8: return "BGRA8";
    case FL_FORMAT_RGBA8: return "RGBA8";
    case FL_FORMAT_RGB10A2: return "RGB10A2";
    case FL_FORMAT_NV12: return "NV12";
    }
    return "?";
}

// ---- GPU work ---------------------------------------------------------------

struct Gpu {
    Vk* vk;
    Dev* dev;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    Gpu(Vk& v, Dev& d) : vk(&v), dev(&d) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = d.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        CHECK(v.AllocateCommandBuffers(d.device, &ai, &cmd) == VK_SUCCESS);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CHECK(v.CreateFence(d.device, &fi, nullptr, &fence) == VK_SUCCESS);
    }
    ~Gpu() { vk->DestroyFence(dev->device, fence, nullptr); }
    void begin() {
        CHECK(vk->ResetCommandBuffer(cmd, 0) == VK_SUCCESS);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK(vk->BeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    }
    void barriers(const VkImageMemoryBarrier* b, uint32_t n, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        vk->CmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, n, b);
    }
    void waitEvent(VkEvent e) {
        vk->CmdWaitEvents(cmd, 1, &e, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, nullptr, 0, nullptr, 0, nullptr);
    }
    // Submit with optional wait/signal ops (timeline values ignored for binary).
    void submit(const flVkSemaphoreOp* wait, const flVkSemaphoreOp* signal) {
        CHECK(vk->EndCommandBuffer(cmd) == VK_SUCCESS);
        CHECK(vk->ResetFences(dev->device, 1, &fence) == VK_SUCCESS);
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        uint64_t waitValue = wait ? wait->value : 0, signalValue = signal ? signal->value : 0;
        VkTimelineSemaphoreSubmitInfo tl{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.pNext = &tl;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        if (wait && wait->semaphore) {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &wait->semaphore;
            si.pWaitDstStageMask = &stage;
            tl.waitSemaphoreValueCount = 1;
            tl.pWaitSemaphoreValues = &waitValue;
        }
        if (signal && signal->semaphore) {
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &signal->semaphore;
            tl.signalSemaphoreValueCount = 1;
            tl.pSignalSemaphoreValues = &signalValue;
        }
        CHECK(vk->QueueSubmit(dev->queue, 1, &si, fence) == VK_SUCCESS);
    }
    bool done() { return vk->GetFenceStatus(dev->device, fence) == VK_SUCCESS; }
    void wait() { CHECK(vk->WaitForFences(dev->device, 1, &fence, VK_TRUE, 5000000000ull) == VK_SUCCESS); }
};

VkBufferImageCopy region(const flVkImage& img, uint32_t index, VkImageAspectFlags aspect,
                         uint32_t w, uint32_t h, VkDeviceSize offset) {
    (void)img; (void)index;
    VkBufferImageCopy r{};
    r.bufferOffset = offset;
    r.imageSubresource.aspectMask = aspect;
    r.imageSubresource.layerCount = 1;
    r.imageExtent = {w, h, 1};
    return r;
}

// Record writes of pattern p into img (TRANSFER_DST), bracketed by ownership
// transfers. `staging` holds NV12 plane bytes.
void recordWrite(Gpu& gpu, const flVkImage& img, const Pattern& p, HostBuffer& staging) {
    VkImageMemoryBarrier b[2];
    uint32_t n = flVkAcquireBarriers(&img, gpu.dev->family, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_ACCESS_TRANSFER_WRITE_BIT, 1, b);
    gpu.barriers(b, n, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (img.format == FL_FORMAT_NV12) {
        size_t ySize = (size_t)img.width * img.height, uvSize = ySize / 2;
        CHECK(staging.size >= ySize + uvSize);
        std::memset(staging.data, p.y, ySize);
        for (size_t i = 0; i < uvSize; i += 2) { staging.data[ySize + i] = p.u; staging.data[ySize + i + 1] = p.v; }
        if (img.imageCount == 2) {
            VkBufferImageCopy r0 = region(img, 0, VK_IMAGE_ASPECT_COLOR_BIT, img.width, img.height, 0);
            VkBufferImageCopy r1 = region(img, 1, VK_IMAGE_ASPECT_COLOR_BIT, img.width / 2, img.height / 2, ySize);
            gpu.vk->CmdCopyBufferToImage(gpu.cmd, staging.buffer, img.image[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r0);
            gpu.vk->CmdCopyBufferToImage(gpu.cmd, staging.buffer, img.image[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r1);
        } else {
            VkBufferImageCopy r[2] = {
                region(img, 0, VK_IMAGE_ASPECT_PLANE_0_BIT, img.width, img.height, 0),
                region(img, 0, VK_IMAGE_ASPECT_PLANE_1_BIT, img.width / 2, img.height / 2, ySize)};
            gpu.vk->CmdCopyBufferToImage(gpu.cmd, staging.buffer, img.image[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2, r);
        }
    } else {
        VkClearColorValue c = clearValue(img.viewFormat[0], p);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        gpu.vk->CmdClearColorImage(gpu.cmd, img.image[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &range);
    }
    n = flVkReleaseBarriers(&img, gpu.dev->family, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_ACCESS_TRANSFER_WRITE_BIT, b);
    gpu.barriers(b, n, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

// Record a copy of img into readback (tightly packed planes).
void recordRead(Gpu& gpu, const flVkImage& img, HostBuffer& readback) {
    VkImageMemoryBarrier b[2];
    uint32_t n = flVkAcquireBarriers(&img, gpu.dev->family, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     VK_ACCESS_TRANSFER_READ_BIT, 0, b);
    gpu.barriers(b, n, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    size_t ySize = (size_t)img.width * img.height;
    if (img.format == FL_FORMAT_NV12 && img.imageCount == 2) {
        VkBufferImageCopy r0 = region(img, 0, VK_IMAGE_ASPECT_COLOR_BIT, img.width, img.height, 0);
        VkBufferImageCopy r1 = region(img, 1, VK_IMAGE_ASPECT_COLOR_BIT, img.width / 2, img.height / 2, ySize);
        gpu.vk->CmdCopyImageToBuffer(gpu.cmd, img.image[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &r0);
        gpu.vk->CmdCopyImageToBuffer(gpu.cmd, img.image[1], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &r1);
    } else if (img.format == FL_FORMAT_NV12) {
        VkBufferImageCopy r[2] = {
            region(img, 0, VK_IMAGE_ASPECT_PLANE_0_BIT, img.width, img.height, 0),
            region(img, 0, VK_IMAGE_ASPECT_PLANE_1_BIT, img.width / 2, img.height / 2, ySize)};
        gpu.vk->CmdCopyImageToBuffer(gpu.cmd, img.image[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 2, r);
    } else {
        VkBufferImageCopy r = region(img, 0, VK_IMAGE_ASPECT_COLOR_BIT, img.width, img.height, 0);
        gpu.vk->CmdCopyImageToBuffer(gpu.cmd, img.image[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &r);
    }
    n = flVkReleaseBarriers(&img, gpu.dev->family, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, b);
    gpu.barriers(b, n, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

// Compare tightly packed bytes against pattern; returns mismatch description.
std::string compare(flFormat f, VkFormat vf, uint32_t w, uint32_t h, const Pattern& p,
                    const uint8_t* plane0, size_t stride0, const uint8_t* plane1, size_t stride1) {
    char msg[160];
    if (f == FL_FORMAT_NV12) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                if (plane0[y * stride0 + x] != p.y) {
                    std::snprintf(msg, sizeof msg, "Y(%u,%u)=%u want %u", x, y, plane0[y * stride0 + x], p.y);
                    return msg;
                }
        for (uint32_t y = 0; y < h / 2; ++y)
            for (uint32_t x = 0; x < w / 2; ++x) {
                const uint8_t* uv = plane1 + y * stride1 + 2 * x;
                if (uv[0] != p.u || uv[1] != p.v) {
                    std::snprintf(msg, sizeof msg, "UV(%u,%u)=%u,%u want %u,%u", x, y, uv[0], uv[1], p.u, p.v);
                    return msg;
                }
            }
        return {};
    }
    uint8_t want[4];
    pixelBytes(vf, p, want);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* px = plane0 + y * stride0 + 4 * x;
            if (std::memcmp(px, want, 4)) {
                std::snprintf(msg, sizeof msg, "(%u,%u)=%02x%02x%02x%02x want %02x%02x%02x%02x", x, y,
                              px[0], px[1], px[2], px[3], want[0], want[1], want[2], want[3]);
                return msg;
            }
        }
    return {};
}

std::string compareReadback(const flVkImage& img, const HostBuffer& rb, const Pattern& p) {
    size_t ySize = (size_t)img.width * img.height;
    if (img.format == FL_FORMAT_NV12)
        return compare(img.format, VK_FORMAT_UNDEFINED, img.width, img.height, p, rb.data, img.width,
                       rb.data + ySize, img.width);
    return compare(img.format, img.viewFormat[0], img.width, img.height, p, rb.data, (size_t)img.width * 4, nullptr, 0);
}

// CPU producer write / consumer read through flImageMapPlanes.
void cpuWrite(flImage* image, flFormat f, uint32_t w, uint32_t h, const Pattern& p) {
    flMappedImage m{};
    CHECK(flImageMapPlanes(image, &m) == FL_OK);
    if (f == FL_FORMAT_NV12) {
        for (uint32_t y = 0; y < h; ++y) std::memset((uint8_t*)m.plane[0].pixels + y * m.plane[0].stride, p.y, w);
        for (uint32_t y = 0; y < h / 2; ++y) {
            uint8_t* row = (uint8_t*)m.plane[1].pixels + y * m.plane[1].stride;
            for (uint32_t x = 0; x < w / 2; ++x) { row[2 * x] = p.u; row[2 * x + 1] = p.v; }
        }
    } else {
        uint8_t px[4];
        pixelBytes(cpuFormat(f), p, px);
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t* row = (uint8_t*)m.plane[0].pixels + y * m.plane[0].stride;
            for (uint32_t x = 0; x < w; ++x) std::memcpy(row + 4 * x, px, 4);
        }
    }
    flImageUnmap(image);
}

std::string cpuCompare(flImage* image, flFormat f, uint32_t w, uint32_t h, const Pattern& p) {
    flMappedImage m{};
    if (flImageMapPlanes(image, &m) != FL_OK) return "flImageMapPlanes failed";
    std::string r = compare(f, cpuFormat(f), w, h, p, (const uint8_t*)m.plane[0].pixels, (size_t)m.plane[0].stride,
                            m.planes > 1 ? (const uint8_t*)m.plane[1].pixels : nullptr,
                            m.planes > 1 ? (size_t)m.plane[1].stride : 0);
    flImageUnmap(image);
    return r;
}

std::string channelName(const char* tag) {
    char name[96];
    std::snprintf(name, sizeof name, "fl-vulkan-%s-%lld", tag,
                  (long long)std::chrono::steady_clock::now().time_since_epoch().count());
    return name;
}

flChannel* openProducer(const std::string& name) {
    flChannel* p = nullptr;
    for (int i = 0; i < 400 && !p; ++i) {
        if (flOpenProducer(name.c_str(), &p) == FL_OK) break;
        p = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(p);
    return p;
}

// Imports cached per slot; re-imported when the ring generation changes.
struct ImportCache {
    Vk* vk;
    Dev* dev;
    std::map<uint32_t, flVkImage> slots;
    ImportCache(Vk& v, Dev& d) : vk(&v), dev(&d) {}
    ~ImportCache() { clear(); }
    void clear() {
        vk->DeviceWaitIdle(dev->device);
        for (auto& s : slots) flVkDestroyImage(&dev->ctx, &s.second);
        slots.clear();
    }
    flResult get(flChannel* ch, uint32_t slot, flImage* image, const flVkImportDesc& desc, const flVkImage** out) {
        auto it = slots.find(slot);
        if (it != slots.end() && !flVkImageCurrent(ch, &it->second)) { clear(); it = slots.end(); }
        if (it == slots.end()) {
            flVkImage img;
            flResult r = flVkImportImage(&dev->ctx, ch, image, &desc, &img);
            if (r != FL_OK) return r;
            it = slots.emplace(slot, img).first;
        }
        *out = &it->second;
        return FL_OK;
    }
};

flVkImportDesc producerDesc() {
    flVkImportDesc d{};
    d.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    d.flags = FL_VK_IMPORT_NO_VIEW;
    return d;
}

flVkImportDesc consumerDesc(Vk& vk, flFormat f) {
    flVkImportDesc d{};
    d.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // Also exercise view + YCbCr conversion creation where the device can.
    if (f != FL_FORMAT_NV12 || vk.ycbcr) d.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    return d;
}

// ---- --formats ----------------------------------------------------------------

enum class Path { VkToCpu, VkToVkGpuSync, VkToVkDefer, CpuToVk };
const char* pathName(Path p) {
    switch (p) {
    case Path::VkToCpu: return "vulkan->cpu";
    case Path::VkToVkGpuSync: return "vulkan->vulkan(GPU_SYNC)";
    case Path::VkToVkDefer: return "vulkan->vulkan(DEFER_RELEASE)";
    case Path::CpuToVk: return "cpu->vulkan(GPU_SYNC)";
    }
    return "?";
}

// Returns false (with reason) when the driver cannot import this combination.
bool runPath(Vk& vk, flFormat format, Path path, std::string& why) {
    const uint32_t W = 64, H = 48, frames = 6;
    uint32_t flags = 0;
    switch (path) {
    case Path::VkToCpu: flags = FL_MAP_CPU; break;
    case Path::VkToVkGpuSync: flags = FL_GPU_SYNC; break;
    case Path::VkToVkDefer: flags = FL_DEFER_RELEASE; break;
    case Path::CpuToVk: flags = FL_GPU_SYNC | FL_MAP_CPU; break;
    }
    // The consumer asks for a Vulkan-importable ring; the negative control
    // drops the request (Linux/NVIDIA tiled RGB imports must then be refused).
    const char* negative = std::getenv("FL_VK_NEGATIVE");
    if (!(negative && !std::strcmp(negative, "no-import-flag"))) flags |= FL_VULKAN_IMPORT;
    std::string name = channelName(formatName(format));
    flChannel* c = nullptr;
    flResult created = flCreateChannelEx(name.c_str(), W, H, format, 3, flags, &c);
    if (created == FL_FORMAT_UNSUPPORTED) { why = "channel format unsupported"; return false; }
    CHECK(created == FL_OK);
    flChannel* p = openProducer(name);
    bool vkProducer = path != Path::CpuToVk, vkConsumer = path != Path::VkToCpu;
    flVkSync psync{}, csync{};
    CHECK(flVkCreateSync(&vk.prod.ctx, p, &psync) == FL_OK);
    CHECK(flVkCreateSync(&vk.cons.ctx, c, &csync) == FL_OK);
    CHECK(psync.producer && !csync.producer);
    VkSemaphore readyBinary = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    CHECK(vk.CreateSemaphore(vk.cons.device, &sci, nullptr, &readyBinary) == VK_SUCCESS);
    HostBuffer staging = makeHostBuffer(vk, vk.prod, (VkDeviceSize)W * H * 4);
    HostBuffer readback = makeHostBuffer(vk, vk.cons, (VkDeviceSize)W * H * 4);
    bool ok = true;
    {
        ImportCache producerImports(vk, vk.prod), consumerImports(vk, vk.cons);
        Gpu pg(vk, vk.prod), cg(vk, vk.cons);
        for (uint32_t k = 1; k <= frames && ok; ++k) {
            Pattern pat = pattern(k);
            flBuffer b{};
            CHECK(flAcquireBuffer(p, &b, 3000) == FL_OK);
            if (vkProducer) {
                const flVkImage* img = nullptr;
                flResult r = producerImports.get(p, b.slot, b.image, producerDesc(), &img);
                if (r != FL_OK) {
                    why = std::string("producer import: ") + flResultString(r);
                    CHECK(flCancelBuffer(p, &b) == FL_OK);
                    ok = false;
                    break;
                }
                pg.begin();
                recordWrite(pg, *img, pat, staging);
                flVkSemaphoreOp signal;
                CHECK(flVkBeginSubmit(&vk.prod.ctx, &psync, &b, &signal) == FL_OK);
                CHECK(signal.semaphore != VK_NULL_HANDLE);
                flVkSemaphoreOp again;
                CHECK(flVkBeginSubmit(&vk.prod.ctx, &psync, &b, &again) == FL_BUSY); // one transaction
                pg.submit(nullptr, &signal);
                CHECK(flVkEndSubmit(&vk.prod.ctx, &psync, &b, (int64_t)k) == FL_OK);
            } else {
                cpuWrite(b.image, format, W, H, pat);
                CHECK(flSubmit(p, &b, (int64_t)k) == FL_OK);
            }
            flFrame f{};
            CHECK(flAcquireFrame(c, &f, 3000) == FL_OK);
            CHECK(f.ptsNs == (int64_t)k);
            std::string bad;
            if (vkConsumer) {
                const flVkImage* img = nullptr;
                flResult r = consumerImports.get(c, f.slot, f.image, consumerDesc(vk, format), &img);
                if (r == FL_FORMAT_UNSUPPORTED && format == FL_FORMAT_NV12 && vk.ycbcr) {
                    // Android native YUV is an external format: SAMPLED only, so
                    // pixels need a shader. Prove the import, view and YCbCr
                    // conversion instead, and report it as import-only.
                    flVkImportDesc sampled{};
                    sampled.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                    flVkImage probe;
                    r = flVkImportImage(&vk.cons.ctx, c, f.image, &sampled, &probe);
                    if (r == FL_OK) {
                        CHECK(probe.view[0] && probe.ycbcr);
                        why = probe.externalFormat ? "import-only: external format (sampled; no transfer readback)"
                                                   : "import-only: sampled";
                        flVkDestroyImage(&vk.cons.ctx, &probe);
                        CHECK(flRelease(c, &f) == FL_OK);
                        ok = false;
                        break;
                    }
                }
                if (r != FL_OK) {
                    why = std::string("consumer import: ") + flResultString(r);
                    CHECK(flRelease(c, &f) == FL_OK);
                    ok = false;
                    if (vkProducer) pg.wait();
                    break;
                }
                CHECK(!(format == FL_FORMAT_NV12 && img->imageCount == 1 && (img->usage & VK_IMAGE_USAGE_SAMPLED_BIT)) ||
                      img->ycbcr != VK_NULL_HANDLE);
                CHECK(!(img->usage & VK_IMAGE_USAGE_SAMPLED_BIT) || img->view[0] != VK_NULL_HANDLE);
                flVkSemaphoreOp wait, signal;
                CHECK(flVkFrameReady(&vk.cons.ctx, &csync, &f, readyBinary, &wait) == FL_OK);
                CHECK((flags & FL_GPU_SYNC) || wait.semaphore == VK_NULL_HANDLE);
                CHECK(flVkFrameConsumed(&vk.cons.ctx, &csync, &f, &signal) == FL_OK);
                flVkSemaphoreOp again;
                CHECK(flVkFrameConsumed(&vk.cons.ctx, &csync, &f, &again) == FL_BUSY); // one signal in flight
                cg.begin();
                recordRead(cg, *img, readback);
                cg.submit(&wait, &signal);
                CHECK(flVkFrameRelease(&vk.cons.ctx, &csync, &f) == FL_OK);
                cg.wait();
                bad = compareReadback(*img, readback, pat);
            } else {
                bad = cpuCompare(f.image, format, W, H, pat);
                CHECK(flRelease(c, &f) == FL_OK);
            }
            if (vkProducer) pg.wait();
            if (!bad.empty()) {
                std::fprintf(stderr, "FAIL %s %s frame %u: %s\n", formatName(format), pathName(path), k, bad.c_str());
                failNow();
            }
        }
    }
    vk.DeviceWaitIdle(vk.prod.device);
    vk.DeviceWaitIdle(vk.cons.device);
    freeHostBuffer(vk, staging);
    freeHostBuffer(vk, readback);
    vk.DestroySemaphore(vk.cons.device, readyBinary, nullptr);
    flVkDestroySync(&vk.prod.ctx, &psync);
    flVkDestroySync(&vk.cons.ctx, &csync);
    flClose(p);
    flClose(c);
    return ok;
}

int formatsTest(Vk& vk) {
    const flFormat formats[] = {FL_FORMAT_BGRA8, FL_FORMAT_RGBA8, FL_FORMAT_RGB10A2, FL_FORMAT_NV12};
    const Path paths[] = {Path::VkToCpu, Path::VkToVkGpuSync, Path::VkToVkDefer, Path::CpuToVk};
    int passed = 0, skipped = 0;
    bool rgbaAll = true;
    for (flFormat f : formats)
        for (Path path : paths) {
            std::string why;
            if (runPath(vk, f, path, why)) {
                std::printf("pass %-8s %s\n", formatName(f), pathName(path));
                ++passed;
            } else {
                std::printf("skip %-8s %s: %s\n", formatName(f), pathName(path), why.c_str());
                ++skipped;
                if (f == FL_FORMAT_RGBA8) rgbaAll = false;
            }
        }
    std::printf("formats: %d passed, %d unsupported by this driver\n", passed, skipped);
    // RGBA8 is the one format every platform's native storage and every
    // conformant driver can import for transfer; it must work everywhere.
    CHECK(rgbaAll);
    return 0;
}

// ---- --ordering -----------------------------------------------------------------

int orderingTest(Vk& vk) {
    const char* negative = std::getenv("FL_VK_NEGATIVE");
    bool skipReadyWait = negative && !std::strcmp(negative, "skip-ready-wait");
    bool earlySignal = negative && !std::strcmp(negative, "early-consumed-signal");
    if (negative) std::printf("NEGATIVE CONTROL: %s (this run must fail)\n", negative);
    const uint32_t W = 32, H = 32;
    std::string name = channelName("ordering");
    flChannel* c = nullptr;
    CHECK(flCreateChannelEx(name.c_str(), W, H, FL_FORMAT_RGBA8, 1, FL_GPU_SYNC | FL_VULKAN_IMPORT, &c) == FL_OK);
    flChannel* p = openProducer(name);
    flVkSync psync{}, csync{};
    CHECK(flVkCreateSync(&vk.prod.ctx, p, &psync) == FL_OK);
    CHECK(flVkCreateSync(&vk.cons.ctx, c, &csync) == FL_OK);
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore readyBinary = VK_NULL_HANDLE;
    CHECK(vk.CreateSemaphore(vk.cons.device, &sci, nullptr, &readyBinary) == VK_SUCCESS);
    VkEventCreateInfo eci{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
    VkEvent producerGate = VK_NULL_HANDLE, consumerGate = VK_NULL_HANDLE;
    CHECK(vk.CreateEvent(vk.prod.device, &eci, nullptr, &producerGate) == VK_SUCCESS);
    CHECK(vk.CreateEvent(vk.cons.device, &eci, nullptr, &consumerGate) == VK_SUCCESS);
    g_onFail.push_back([&] {
        vk.SetEvent(vk.prod.device, producerGate);
        vk.SetEvent(vk.cons.device, consumerGate);
    });
    HostBuffer staging = makeHostBuffer(vk, vk.prod, (VkDeviceSize)W * H * 4);
    HostBuffer readback = makeHostBuffer(vk, vk.cons, (VkDeviceSize)W * H * 4);
    {
        ImportCache producerImports(vk, vk.prod), consumerImports(vk, vk.cons);
        Gpu pg(vk, vk.prod), cg(vk, vk.cons), early(vk, vk.cons);
        auto produce = [&](uint32_t k, bool gated) {
            flBuffer b{};
            CHECK(flAcquireBuffer(p, &b, 3000) == FL_OK);
            const flVkImage* img = nullptr;
            CHECK(producerImports.get(p, b.slot, b.image, producerDesc(), &img) == FL_OK);
            pg.begin();
            if (gated) pg.waitEvent(producerGate);
            recordWrite(pg, *img, pattern(k), staging);
            flVkSemaphoreOp signal;
            CHECK(flVkBeginSubmit(&vk.prod.ctx, &psync, &b, &signal) == FL_OK);
            pg.submit(nullptr, &signal);
            CHECK(flVkEndSubmit(&vk.prod.ctx, &psync, &b, (int64_t)k) == FL_OK);
        };
        auto consume = [&](bool gated, flFrame& f, const flVkImage*& img) {
            CHECK(flAcquireFrame(c, &f, 3000) == FL_OK);
            CHECK(consumerImports.get(c, f.slot, f.image, consumerDesc(vk, FL_FORMAT_RGBA8), &img) == FL_OK);
            flVkSemaphoreOp wait, signal;
            CHECK(flVkFrameReady(&vk.cons.ctx, &csync, &f, readyBinary, &wait) == FL_OK);
            if (skipReadyWait) wait.semaphore = VK_NULL_HANDLE; // bug under test: read races the producer
            CHECK(flVkFrameConsumed(&vk.cons.ctx, &csync, &f, &signal) == FL_OK);
            if (earlySignal) {
                // Bug under test: completion signalled by a submit that does not
                // contain (or follow) the reads.
                early.begin();
                early.submit(nullptr, &signal);
                cg.begin();
                if (gated) cg.waitEvent(consumerGate);
                recordRead(cg, *img, readback);
                cg.submit(&wait, nullptr);
            } else {
                cg.begin();
                if (gated) cg.waitEvent(consumerGate);
                recordRead(cg, *img, readback);
                cg.submit(&wait, &signal);
            }
            CHECK(flVkFrameRelease(&vk.cons.ctx, &csync, &f) == FL_OK);
        };
        const auto settle = std::chrono::milliseconds(150);
        // Phase 1 - readiness. The producer's GPU write is held behind a host
        // event; the consumer's GPU_SYNC acquire returns at once, and its read
        // must not run until the producer's work completes.
        flFrame f{};
        const flVkImage* img = nullptr;
        produce(1, true);
        consume(false, f, img);
        std::this_thread::sleep_for(settle);
        CHECK(!cg.done()); // consumer GPU read is waiting on the producer's ready fence
        CHECK(vk.SetEvent(vk.prod.device, producerGate) == VK_SUCCESS);
        cg.wait();
        pg.wait();
        std::string bad = compareReadback(*img, readback, pattern(1));
        if (!bad.empty()) { std::fprintf(stderr, "FAIL readiness content: %s\n", bad.c_str()); failNow(); }
        std::puts("readiness: consumer GPU read waited for producer GPU write");
        // Phase 2 - completion. Pool of one: the producer cannot get the slot
        // back until the consumer's GPU read (held behind its own event) ends.
        produce(2, false);
        pg.wait();
        consume(true, f, img);
        flBuffer b{};
        std::this_thread::sleep_for(settle);
        CHECK(flAcquireBuffer(p, &b, 100) == FL_TIMEOUT); // slot still being read on the consumer's GPU
        CHECK(!cg.done());
        CHECK(vk.SetEvent(vk.cons.device, consumerGate) == VK_SUCCESS);
        CHECK(flAcquireBuffer(p, &b, 3000) == FL_OK);
        cg.wait();
        bad = compareReadback(*img, readback, pattern(2));
        if (!bad.empty()) { std::fprintf(stderr, "FAIL completion content: %s\n", bad.c_str()); failNow(); }
        CHECK(flCancelBuffer(p, &b) == FL_OK);
        std::puts("completion: producer reuse waited for consumer GPU read");
        if (earlySignal) early.wait();
    }
    vk.DeviceWaitIdle(vk.prod.device);
    vk.DeviceWaitIdle(vk.cons.device);
    freeHostBuffer(vk, staging);
    freeHostBuffer(vk, readback);
    g_onFail.clear();
    vk.DestroyEvent(vk.prod.device, producerGate, nullptr);
    vk.DestroyEvent(vk.cons.device, consumerGate, nullptr);
    vk.DestroySemaphore(vk.cons.device, readyBinary, nullptr);
    flVkDestroySync(&vk.prod.ctx, &psync);
    flVkDestroySync(&vk.cons.ctx, &csync);
    flClose(p);
    flClose(c);
    return 0;
}

// ---- --adapter / --generation -----------------------------------------------------

int adapterTest(Vk& vk, flChannel* ring) {
    flChannelInfo info{};
    CHECK(flQuery(ring, &info) == FL_OK);
    CHECK(flVkCheckAdapter(&vk.cons.ctx, &info) == FL_OK);
    CHECK(flVkCheckAdapter(nullptr, &info) == FL_INVALID);
    CHECK(flVkCheckAdapter(&vk.cons.ctx, nullptr) == FL_INVALID);
    info.adapterLUID[0] ^= 0x5a;
#if defined(__ANDROID__)
    CHECK(flVkCheckAdapter(&vk.cons.ctx, &info) == FL_OK); // one GPU; nothing to compare
#else
    CHECK(flVkCheckAdapter(&vk.cons.ctx, &info) == FL_ADAPTER_MISMATCH);
#endif
    std::puts("adapter: ring GPU accepted, altered identity rejected");
    return 0;
}

int generationTest(Vk& vk) {
    std::string name = channelName("generation");
    flChannel* c = nullptr;
    CHECK(flCreateChannelEx(name.c_str(), 64, 32, FL_FORMAT_RGBA8, 2, FL_MAP_CPU | FL_VULKAN_IMPORT, &c) == FL_OK);
    flChannel* p = openProducer(name);
    flVkSync psync{};
    CHECK(flVkCreateSync(&vk.prod.ctx, p, &psync) == FL_OK);
    HostBuffer staging = makeHostBuffer(vk, vk.prod, 4);
    {
        ImportCache imports(vk, vk.prod);
        Gpu gpu(vk, vk.prod);
        auto frame = [&](uint32_t k, uint32_t w, uint32_t h) {
            flBuffer b{};
            CHECK(flAcquireBuffer(p, &b, 3000) == FL_OK);
            const flVkImage* img = nullptr;
            CHECK(imports.get(p, b.slot, b.image, producerDesc(), &img) == FL_OK);
            CHECK(img->width == w && img->height == h);
            gpu.begin();
            recordWrite(gpu, *img, pattern(k), staging);
            flVkSemaphoreOp signal;
            CHECK(flVkBeginSubmit(&vk.prod.ctx, &psync, &b, &signal) == FL_OK);
            gpu.submit(nullptr, &signal);
            CHECK(flVkEndSubmit(&vk.prod.ctx, &psync, &b, (int64_t)k) == FL_OK);
            flFrame f{};
            CHECK(flAcquireFrame(c, &f, 3000) == FL_OK);
            std::string bad = cpuCompare(f.image, FL_FORMAT_RGBA8, w, h, pattern(k));
            CHECK(flRelease(c, &f) == FL_OK);
            gpu.wait();
            if (!bad.empty()) { std::fprintf(stderr, "FAIL generation frame %u: %s\n", k, bad.c_str()); failNow(); }
        };
        frame(1, 64, 32);
        frame(2, 64, 32);
        flChannelInfo before{}, after{};
        CHECK(flQuery(p, &before) == FL_OK);
        CHECK(!imports.slots.empty() && flVkImageCurrent(p, &imports.slots.begin()->second));
        CHECK(flRequestGeometry(p, 96, 40, FL_FORMAT_RGBA8) == FL_OK);
        CHECK(flQuery(p, &after) == FL_OK);
        CHECK(after.generation != before.generation && after.width == 96 && after.height == 40);
        CHECK(!flVkImageCurrent(p, &imports.slots.begin()->second));
        frame(3, 96, 40);
        frame(4, 96, 40);
#if defined(_WIN32)
        // The new ring came with new shared fences; the old ones were parked.
        CHECK(psync.retiredCount == 1 && psync.generation == after.generation);
#else
        CHECK(psync.retiredCount == 0); // POSIX fences are per frame
#endif
        vk.DeviceWaitIdle(vk.prod.device);
        flVkSyncCollect(&vk.prod.ctx, &psync);
        CHECK(psync.retiredCount == 0);
    }
    vk.DeviceWaitIdle(vk.prod.device);
    vk.DeviceWaitIdle(vk.cons.device);
    freeHostBuffer(vk, staging);
    flVkDestroySync(&vk.prod.ctx, &psync);
    flClose(p);
    flClose(c);
    std::puts("generation: reallocation invalidated imports; re-import wrote the new ring");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(50));
        std::fprintf(stderr, "FAIL watchdog: test still running after 50 s\n");
        failNow();
    }).detach();
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "--formats" && mode != "--ordering" && mode != "--adapter" && mode != "--generation") {
        std::fprintf(stderr, "usage: %s --formats|--ordering|--adapter|--generation\n", argv[0]);
        return 2;
    }
    // A probe ring names the GPU FrameLink allocates on; Vulkan must match it.
    std::string probeName = channelName("probe");
    flChannel* probe = nullptr;
    CHECK(flCreateChannelEx(probeName.c_str(), 16, 16, FL_FORMAT_RGBA8, 1, 0, &probe) == FL_OK);
    flChannelInfo ring{};
    CHECK(flQuery(probe, &ring) == FL_OK);
    Vk vk;
    initVulkan(vk, ring);
    int rc = 0;
    if (mode == "--formats") rc = formatsTest(vk);
    else if (mode == "--ordering") rc = orderingTest(vk);
    else if (mode == "--adapter") rc = adapterTest(vk, probe);
    else rc = generationTest(vk);
    flClose(probe);
    destroyVulkan(vk);
    if (rc == 0) std::printf("PASS %s\n", mode.c_str());
    return rc;
}
