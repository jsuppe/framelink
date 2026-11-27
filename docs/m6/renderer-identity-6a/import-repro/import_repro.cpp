// Standalone check for the 6a black BGRA import (FrameLink decision 12 open
// finding): no FrameLink, no vkrender. Allocates N linear ARGB8888 GBM buffers
// as FrameLink's FL_MAP_CPU rings do, writes each on the CPU through its
// dma-buf mmap inside DMA_BUF_IOCTL_SYNC, imports each into Vulkan as
// vkrender's ExternalVideoPool does (explicit LINEAR modifier,
// B8G8R8A8_UNORM, device-local type, dedicated only if preferred), then copies
// a 16x9 grid of every image back on the GPU and compares with what was written.
//
//   import_repro [--buffers N] [--rounds R] [--order fill-import|import-fill]
//                [--separate-gbm] [--pipeline-after-first]
//
// Rounds after the first rewrite every buffer with new colours (a producer's
// next frames) and read all imports again.
#include <gbm.h>
#include <vulkan/vulkan.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(2); } } while (0)

static const uint32_t W = 1920, H = 1080;

struct Buf { gbm_bo* bo; int fd; uint32_t stride, offset; size_t size; void* map; };
struct Imp { VkImage image; VkDeviceMemory memory; };

static gbm_device* openGbm() {
    for (int i = 128; i < 136; ++i) {
        char path[64]; snprintf(path, sizeof path, "/dev/dri/renderD%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        if (gbm_device* g = gbm_create_device(fd)) { fprintf(stderr, "gbm: %s (%s)\n", path, gbm_device_get_backend_name(g)); return g; }
        close(fd);
    }
    return nullptr;
}

static uint32_t colourFor(uint32_t buffer, uint32_t round) {
    // Distinct, never dark: BGRA bytes in memory, ARGB8888 little-endian word.
    const uint32_t r = 64 + (buffer * 37 + round * 91) % 192, g = 64 + (buffer * 71 + round * 53) % 192, b = 64 + (buffer * 13 + round * 29) % 192;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void fill(Buf& b, uint32_t colour) {
    dma_buf_sync s{DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
    ioctl(b.fd, DMA_BUF_IOCTL_SYNC, &s);
    for (uint32_t y = 0; y < H; ++y) {
        auto* row = (uint32_t*)((char*)b.map + b.offset + (size_t)y * b.stride);
        for (uint32_t x = 0; x < W; ++x) row[x] = colour;
    }
    s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    ioctl(b.fd, DMA_BUF_IOCTL_SYNC, &s);
}

int main(int argc, char** argv) {
    uint32_t n = 7, rounds = 3; std::string order = "fill-import"; bool separateGbm = false, pipelineAfterFirst = false, reqSize = false, forceDedicated = false, reverse = false, dupFirst = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--buffers" && i + 1 < argc) n = atoi(argv[++i]);
        else if (a == "--rounds" && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (a == "--order" && i + 1 < argc) order = argv[++i];
        else if (a == "--separate-gbm") separateGbm = true;
        else if (a == "--pipeline-after-first") pipelineAfterFirst = true;
        else if (a == "--alloc-req-size") reqSize = true;
        else if (a == "--force-dedicated") forceDedicated = true;
        else if (a == "--reverse") reverse = true;
        else if (a == "--dup-first") dupFirst = true;
        else { fprintf(stderr, "unknown %s\n", a.c_str()); return 1; }
    }

    // ---- GBM buffers, FrameLink's FL_MAP_CPU allocation ----
    gbm_device* shared = separateGbm ? nullptr : openGbm();
    std::vector<Buf> bufs(n);
    for (uint32_t i = 0; i < n; ++i) {
        gbm_device* g = separateGbm ? openGbm() : shared;
        if (!g) { fprintf(stderr, "no gbm device\n"); return 1; }
        Buf& b = bufs[i];
        b.bo = gbm_bo_create(g, W, H, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
        if (!b.bo) b.bo = gbm_bo_create(g, W, H, GBM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR);
        if (!b.bo || gbm_bo_get_modifier(b.bo) != 0) { fprintf(stderr, "gbm_bo_create failed or not linear\n"); return 1; }
        b.fd = gbm_bo_get_fd_for_plane(b.bo, 0);
        b.stride = gbm_bo_get_stride_for_plane(b.bo, 0);
        b.offset = gbm_bo_get_offset(b.bo, 0);
        b.size = (size_t)lseek(b.fd, 0, SEEK_END);
        b.map = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, b.fd, 0);
        if (b.map == MAP_FAILED) { perror("mmap"); return 1; }
    }
    fprintf(stderr, "%u buffers, stride %u offset %u size %zu\n", n, bufs[0].stride, bufs[0].offset, bufs[0].size);

    // ---- Vulkan ----
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    VkInstance inst; CHECK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t pc = 0; vkEnumeratePhysicalDevices(inst, &pc, nullptr);
    std::vector<VkPhysicalDevice> pds(pc); vkEnumeratePhysicalDevices(inst, &pc, pds.data());
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = p; fprintf(stderr, "device: %s driver 0x%x\n", pr.deviceName, pr.driverVersion); break; } }
    if (!phys) { fprintf(stderr, "no discrete GPU\n"); return 1; }
    uint32_t qf = 0; // graphics family 0 on NVIDIA
    float prio = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = qf; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* exts[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                          VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext = &f13;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 4; dci.ppEnabledExtensionNames = exts;
    VkDevice dev; CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
    VkQueue q; vkGetDeviceQueue(dev, qf, 0, &q);
    auto getFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR");
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    auto pick = [&](uint32_t bits, VkMemoryPropertyFlags want, bool preferLocal) {
        int any = -1;
        for (uint32_t t = 0; t < mp.memoryTypeCount; ++t) {
            if (!(bits & (1u << t)) || (mp.memoryTypes[t].propertyFlags & want) != want) continue;
            if (preferLocal && (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return (int)t;
            if (any < 0) any = (int)t;
        }
        return any;
    };

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpci.queueFamilyIndex = qf; cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool; CHECK(vkCreateCommandPool(dev, &cpci, nullptr, &pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cbai.commandPool = pool; cbai.commandBufferCount = 1;
    VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));

    // Readback buffer: 144 pixels.
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size = 144 * 4; bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer rb; CHECK(vkCreateBuffer(dev, &bci, nullptr, &rb));
    VkMemoryRequirements rbr; vkGetBufferMemoryRequirements(dev, rb, &rbr);
    VkMemoryAllocateInfo rba{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; rba.allocationSize = rbr.size;
    rba.memoryTypeIndex = pick(rbr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
    VkDeviceMemory rbm; CHECK(vkAllocateMemory(dev, &rba, nullptr, &rbm)); CHECK(vkBindBufferMemory(dev, rb, rbm, 0));
    uint32_t* rbp; CHECK(vkMapMemory(dev, rbm, 0, VK_WHOLE_SIZE, 0, (void**)&rbp));

    auto importBuf = [&](Buf& b) {
        Imp m{};
        VkSubresourceLayout layout{}; layout.offset = b.offset; layout.rowPitch = b.stride;
        VkImageDrmFormatModifierExplicitCreateInfoEXT mod{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
        mod.drmFormatModifier = 0; mod.drmFormatModifierPlaneCount = 1; mod.pPlaneLayouts = &layout;
        VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO}; ext.pNext = &mod; ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.pNext = &ext; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_B8G8R8A8_UNORM;
        ic.extent = {W, H, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        CHECK(vkCreateImage(dev, &ic, nullptr, &m.image));
        VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
        CHECK(getFdProps(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, b.fd, &fp));
        VkMemoryDedicatedRequirements dr{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
        VkMemoryRequirements2 r2{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2}; r2.pNext = &dr;
        VkImageMemoryRequirementsInfo2 ri{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2}; ri.image = m.image;
        vkGetImageMemoryRequirements2(dev, &ri, &r2);
        VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR}; imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        imp.fd = fcntl(b.fd, F_DUPFD_CLOEXEC, 0);
        VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; ded.pNext = &imp; ded.image = m.image;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.pNext = (forceDedicated || dr.prefersDedicatedAllocation || dr.requiresDedicatedAllocation) ? (void*)&ded : (void*)&imp;
        ai.allocationSize = reqSize ? r2.memoryRequirements.size : b.size; ai.memoryTypeIndex = pick(r2.memoryRequirements.memoryTypeBits & fp.memoryTypeBits, 0, true);
        CHECK(vkAllocateMemory(dev, &ai, nullptr, &m.memory));
        CHECK(vkBindImageMemory(dev, m.image, m.memory, 0));
        return m;
    };

    // Something heavy between the first and later imports, as vkrender's
    // first bind creates a sampler, layouts and a graphics pipeline.
    auto heavy = [&] {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; VkSampler s; CHECK(vkCreateSampler(dev, &sci, nullptr, &s));
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.maxSets = 16; dp.poolSizeCount = 1; dp.pPoolSizes = &ps;
        VkDescriptorPool d; CHECK(vkCreateDescriptorPool(dev, &dp, nullptr, &d));
    };

    std::vector<Imp> imps(n);
    if (order == "fill-import") {
        for (uint32_t i = 0; i < n; ++i) fill(bufs[i], colourFor(i, 0));
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t i = reverse ? n - 1 - k : k;
            imps[i] = importBuf(bufs[i]);
            if (k == 0 && pipelineAfterFirst) heavy();
            if (k == 0 && dupFirst) { Imp extra = importBuf(bufs[i]); fprintf(stderr, "extra import of buffer %u (never read)\n", i); (void)extra; }
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) { imps[i] = importBuf(bufs[i]); if (i == 0 && pipelineAfterFirst) heavy(); }
        for (uint32_t i = 0; i < n; ++i) fill(bufs[i], colourFor(i, 0));
    }

    int failures = 0;
    for (uint32_t round = 0; round < rounds; ++round) {
        if (round) for (uint32_t i = 0; i < n; ++i) fill(bufs[i], colourFor(i, round));
        printf("round %u:", round);
        for (uint32_t i = 0; i < n; ++i) {
            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cmd, &bi);
            VkImageMemoryBarrier2 acq{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            acq.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; acq.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            acq.oldLayout = VK_IMAGE_LAYOUT_GENERAL; acq.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            acq.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT; acq.dstQueueFamilyIndex = qf;
            acq.image = imps[i].image; acq.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &acq;
            vkCmdPipelineBarrier2(cmd, &dep);
            VkBufferImageCopy regions[144];
            for (uint32_t gy = 0, k = 0; gy < 9; ++gy) for (uint32_t gx = 0; gx < 16; ++gx, ++k) {
                regions[k] = {};
                regions[k].bufferOffset = k * 4;
                regions[k].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                regions[k].imageOffset = {(int32_t)((2 * gx + 1) * W / 32), (int32_t)((2 * gy + 1) * H / 18), 0};
                regions[k].imageExtent = {1, 1, 1};
            }
            vkCmdCopyImageToBuffer(cmd, imps[i].image, VK_IMAGE_LAYOUT_GENERAL, rb, 144, regions);
            VkImageMemoryBarrier2 rel = acq;
            rel.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; rel.srcAccessMask = 0; rel.dstStageMask = 0; rel.dstAccessMask = 0;
            rel.srcQueueFamilyIndex = qf; rel.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
            dep.pImageMemoryBarriers = &rel; vkCmdPipelineBarrier2(cmd, &dep);
            vkEndCommandBuffer(cmd);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
            CHECK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(q));
            const uint32_t want = colourFor(i, round);
            uint32_t match = 0, zero = 0;
            for (uint32_t k = 0; k < 144; ++k) { match += rbp[k] == want; zero += (rbp[k] & 0xffffff) == 0; }
            const char* verdict = match == 144 ? "ok" : zero == 144 ? "ZERO" : "WRONG";
            if (match != 144) ++failures;
            printf(" [%u %s]", i, verdict);
        }
        printf("\n");
    }
    printf("%s: %d bad reads\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 3 : 0;
}
