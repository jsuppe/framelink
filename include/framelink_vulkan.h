// framelink Vulkan interop - Windows, Linux and Android.
//
// Header only. Nothing here is exported from the framelink library, and the
// library never links Vulkan: every function below is an inline wrapper over
// the platform interop header (framelink_d3d11.h, framelink_dmabuf.h,
// framelink_ahb.h, framelink_sync_fd.h) and calls Vulkan only through function
// pointers loaded from the vkGetInstanceProcAddr you pass in. That makes it
// work with the loader, volk, or any other dispatch.
//
// Include your Vulkan headers first if you use volk or VK_NO_PROTOTYPES; this
// header includes <vulkan/vulkan_core.h> and the platform extension header if
// they are not already present. Vulkan 1.1 is required.
//
// Device extensions, also returned by flVkRequiredDeviceExtensions():
//   Windows  VK_KHR_external_memory_win32, VK_KHR_external_semaphore_win32,
//            and the timelineSemaphore feature (Vulkan 1.2 or the KHR ext)
//   Linux    VK_KHR_external_memory_fd, VK_EXT_external_memory_dma_buf,
//            VK_EXT_image_drm_format_modifier, VK_EXT_queue_family_foreign,
//            VK_KHR_external_semaphore_fd
//   Android  VK_ANDROID_external_memory_android_hardware_buffer,
//            VK_EXT_queue_family_foreign, VK_KHR_external_semaphore_fd
// NV12 sampling also needs the samplerYcbcrConversion feature. Linux adapter
// checks use VK_EXT_physical_device_drm when the device offers it.
//
// The call sequence is the one in framelink.h; these helpers only replace the
// native handle plumbing:
//
//   both      flVkInitContext, flVkCheckAdapter, flVkCreateSync
//             flVkImportImage (once per slot and channel generation)
//   consumer  flAcquireFrame -> flVkFrameReady (wait) -> your submit, which
//             also signals flVkFrameConsumed -> vkQueueSubmit -> flVkFrameRelease
//   producer  flAcquireBuffer -> flVkBeginSubmit (signal) -> vkQueueSubmit ->
//             flVkEndSubmit
//
// Images cross the ring in VK_IMAGE_LAYOUT_GENERAL. Use flVkAcquireBarriers
// before your first access in a submit and flVkReleaseBarriers after your last.
//
// A consumer that waits and signals on the GPU must create its channel with
// FL_GPU_SYNC (GPU readiness wait) or FL_DEFER_RELEASE (FrameLink waits for
// readiness on the CPU, you retire storage on the GPU). A plain consumer has
// nothing to wait for after flAcquireFrame, but must finish its GPU reads
// before flRelease; flVkFrameConsumed refuses it rather than order nothing.
// The producer side needs no flag: flAcquireBuffer already waits for the
// consumer's completion fence before it returns a slot.
//
// The consumer allocates the ring, so the consumer decides whether Vulkan can
// import it: create it with FL_VULKAN_IMPORT when either side will use this
// header. Linux/NVIDIA GPU rings are otherwise compressed and refused here
// (FL_FORMAT_UNSUPPORTED); FL_MAP_CPU rings are linear and always importable.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "framelink.h"

#if defined(_WIN32)
#  include "framelink_d3d11.h"
#elif defined(__ANDROID__)
#  include "framelink_ahb.h"
#  include "framelink_sync_fd.h"
#else
#  include "framelink_dmabuf.h"
#  include "framelink_sync_fd.h"
#  include <sys/types.h>
#  include <sys/stat.h>
#endif
#if !defined(_WIN32)
#  include <unistd.h>
#endif

#ifndef VK_VERSION_1_0
#  include <vulkan/vulkan_core.h>
#endif
#if defined(_WIN32) && !defined(VULKAN_WIN32_H_)
#  include <vulkan/vulkan_win32.h>
#endif
#if defined(__ANDROID__) && !defined(VULKAN_ANDROID_H_)
#  include <vulkan/vulkan_android.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FL_VK_INLINE static inline

// ---- context -------------------------------------------------------------------

// Function pointers for one VkDevice. Fill with flVkInitContext; plain data,
// copyable, nothing to destroy.
typedef struct flVkContext {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    const VkAllocationCallbacks* allocator;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFormatProperties2 GetPhysicalDeviceFormatProperties2;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 GetPhysicalDeviceImageFormatProperties2;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements2 GetImageMemoryRequirements2;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindImageMemory2 BindImageMemory2;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateSamplerYcbcrConversion CreateSamplerYcbcrConversion;
    PFN_vkDestroySamplerYcbcrConversion DestroySamplerYcbcrConversion;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
#if defined(_WIN32)
    PFN_vkGetMemoryWin32HandlePropertiesKHR GetMemoryWin32HandlePropertiesKHR;
    PFN_vkImportSemaphoreWin32HandleKHR ImportSemaphoreWin32HandleKHR;
#else
    PFN_vkImportSemaphoreFdKHR ImportSemaphoreFdKHR;
    PFN_vkGetSemaphoreFdKHR GetSemaphoreFdKHR;
#endif
#if defined(__ANDROID__)
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID GetAndroidHardwareBufferPropertiesANDROID;
#elif !defined(_WIN32)
    PFN_vkGetMemoryFdPropertiesKHR GetMemoryFdPropertiesKHR;
#endif
} flVkContext;

// The device extensions this header needs on this platform (static storage).
FL_VK_INLINE const char* const* flVkRequiredDeviceExtensions(uint32_t* count) {
    static const char* const names[] = {
#if defined(_WIN32)
        "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32",
#elif defined(__ANDROID__)
        "VK_ANDROID_external_memory_android_hardware_buffer",
        "VK_EXT_queue_family_foreign", "VK_KHR_external_semaphore_fd",
#else
        "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf",
        "VK_EXT_image_drm_format_modifier", "VK_EXT_queue_family_foreign",
        "VK_KHR_external_semaphore_fd",
#endif
    };
    if (count) *count = (uint32_t)(sizeof(names) / sizeof(names[0]));
    return names;
}

// Load everything once. FL_FORMAT_UNSUPPORTED when an entry point is missing
// (Vulkan older than 1.1, or a required extension not enabled on the device).
// Output is zeroed on failure.
FL_VK_INLINE flResult flVkInitContext(flVkContext* ctx, VkInstance instance,
    VkPhysicalDevice physicalDevice, VkDevice device,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr, const VkAllocationCallbacks* allocator) {
    if (!ctx) return FL_INVALID;
    memset(ctx, 0, sizeof(*ctx));
    if (!instance || !physicalDevice || !device || !getInstanceProcAddr) return FL_INVALID;
    PFN_vkGetDeviceProcAddr gdpa =
        (PFN_vkGetDeviceProcAddr)getInstanceProcAddr(instance, "vkGetDeviceProcAddr");
    if (!gdpa) return FL_FORMAT_UNSUPPORTED;
    flVkContext c;
    memset(&c, 0, sizeof(c));
    c.instance = instance;
    c.physicalDevice = physicalDevice;
    c.device = device;
    c.allocator = allocator;
    int ok = 1;
#define FL_VK_I(name) ok &= (c.name = (PFN_vk##name)getInstanceProcAddr(instance, "vk" #name)) != NULL
#define FL_VK_D(name) ok &= (c.name = (PFN_vk##name)gdpa(device, "vk" #name)) != NULL
    FL_VK_I(EnumerateDeviceExtensionProperties);
    FL_VK_I(GetPhysicalDeviceMemoryProperties);
    FL_VK_I(GetPhysicalDeviceProperties2);
    FL_VK_I(GetPhysicalDeviceFormatProperties2);
    FL_VK_I(GetPhysicalDeviceImageFormatProperties2);
    FL_VK_D(CreateImage);
    FL_VK_D(DestroyImage);
    FL_VK_D(GetImageMemoryRequirements2);
    FL_VK_D(AllocateMemory);
    FL_VK_D(FreeMemory);
    FL_VK_D(BindImageMemory2);
    FL_VK_D(CreateImageView);
    FL_VK_D(DestroyImageView);
    FL_VK_D(CreateSemaphore);
    FL_VK_D(DestroySemaphore);
    // Optional: only NV12 sampling needs it, and only if the feature is enabled.
    c.CreateSamplerYcbcrConversion =
        (PFN_vkCreateSamplerYcbcrConversion)gdpa(device, "vkCreateSamplerYcbcrConversion");
    c.DestroySamplerYcbcrConversion =
        (PFN_vkDestroySamplerYcbcrConversion)gdpa(device, "vkDestroySamplerYcbcrConversion");
#if defined(_WIN32)
    FL_VK_D(GetMemoryWin32HandlePropertiesKHR);
    FL_VK_D(ImportSemaphoreWin32HandleKHR);
#else
    FL_VK_D(ImportSemaphoreFdKHR);
    FL_VK_D(GetSemaphoreFdKHR);
#endif
#if defined(__ANDROID__)
    FL_VK_D(GetAndroidHardwareBufferPropertiesANDROID);
#elif !defined(_WIN32)
    FL_VK_D(GetMemoryFdPropertiesKHR);
#endif
#undef FL_VK_I
#undef FL_VK_D
    if (!ok) return FL_FORMAT_UNSUPPORTED;
    *ctx = c;
    return FL_OK;
}

FL_VK_INLINE int flVkDeviceHasExtension_(const flVkContext* ctx, const char* name) {
    uint32_t n = 0;
    if (ctx->EnumerateDeviceExtensionProperties(ctx->physicalDevice, NULL, &n, NULL) != VK_SUCCESS || !n)
        return 0;
    VkExtensionProperties* props = (VkExtensionProperties*)malloc(n * sizeof(*props));
    if (!props) return 0;
    int found = 0;
    VkResult r = ctx->EnumerateDeviceExtensionProperties(ctx->physicalDevice, NULL, &n, props);
    if (r == VK_SUCCESS || r == VK_INCOMPLETE)
        for (uint32_t i = 0; i < n && !found; ++i) found = strcmp(props[i].extensionName, name) == 0;
    free(props);
    return found;
}

// Is this the GPU the ring lives on? Pass flQuery's info.
//   FL_OK                  same adapter (Android: always; one GPU)
//   FL_ADAPTER_MISMATCH    a different adapter - imports would fail or worse
//   FL_FORMAT_UNSUPPORTED  cannot tell: Windows device without a valid LUID,
//                          Linux device without VK_EXT_physical_device_drm
// Windows compares VkPhysicalDeviceIDProperties::deviceLUID. Linux compares the
// render node (major, minor) FrameLink reports in adapterLUID.
FL_VK_INLINE flResult flVkCheckAdapter(const flVkContext* ctx, const flChannelInfo* info) {
    if (!ctx || !ctx->physicalDevice || !info) return FL_INVALID;
#if defined(__ANDROID__)
    return FL_OK;
#elif defined(_WIN32)
    VkPhysicalDeviceIDProperties id;
    memset(&id, 0, sizeof(id));
    id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 props;
    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &id;
    ctx->GetPhysicalDeviceProperties2(ctx->physicalDevice, &props);
    if (!id.deviceLUIDValid) return FL_FORMAT_UNSUPPORTED;
    return memcmp(id.deviceLUID, info->adapterLUID, 8) == 0 ? FL_OK : FL_ADAPTER_MISMATCH;
#else
    if (!flVkDeviceHasExtension_(ctx, "VK_EXT_physical_device_drm")) return FL_FORMAT_UNSUPPORTED;
    VkPhysicalDeviceDrmPropertiesEXT drm;
    memset(&drm, 0, sizeof(drm));
    drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 props;
    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &drm;
    ctx->GetPhysicalDeviceProperties2(ctx->physicalDevice, &props);
    if (!drm.hasRender) return FL_ADAPTER_MISMATCH;
    uint32_t major = 0, minor = 0;
    for (int i = 0; i < 4; ++i) {
        major |= (uint32_t)info->adapterLUID[i] << (8 * i);
        minor |= (uint32_t)info->adapterLUID[4 + i] << (8 * i);
    }
    return (uint32_t)drm.renderMajor == major && (uint32_t)drm.renderMinor == minor
               ? FL_OK : FL_ADAPTER_MISMATCH;
#endif
}

// ---- images --------------------------------------------------------------------

// The Vulkan format of a channel's native storage. Android stores BGRA8
// channels as RGBA8 (CPU maps swizzle); its NV12 is usually an external format,
// which flVkImportImage reports as VK_FORMAT_UNDEFINED plus externalFormat.
FL_VK_INLINE VkFormat flVkFormat(flFormat format) {
    switch (format) {
#if defined(__ANDROID__)
    case FL_FORMAT_BGRA8: return VK_FORMAT_R8G8B8A8_UNORM;
#else
    case FL_FORMAT_BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
#endif
    case FL_FORMAT_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    // DXGI/DRM R10G10B10A2: R in bits 0-9 == Vulkan A2B10G10R10 packed.
    case FL_FORMAT_RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case FL_FORMAT_NV12: return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

// flVkImportDesc::flags
// NV12 only: import luma and chroma as two images (R8 w x h, R8G8 w/2 x h/2)
// instead of one multi-planar image. Linux only (D3D11 and AHardwareBuffer
// expose one object); chosen automatically there when the driver cannot use
// the buffer's modifier as multi-planar NV12. Planes render and store like any
// RGB image; the combined form needs a YCbCr sampler and is SAMPLED/transfer.
#define FL_VK_IMPORT_PLANES 0x1u
// Never fall back to per-plane NV12; FL_FORMAT_UNSUPPORTED instead.
#define FL_VK_IMPORT_COMBINED 0x2u
// Do not create image views or a YCbCr conversion.
#define FL_VK_IMPORT_NO_VIEW 0x4u

typedef struct flVkImportDesc {
    // How YOU will use the image: SAMPLED, TRANSFER_SRC/DST, COLOR_ATTACHMENT,
    // STORAGE. Checked against the driver for this exact buffer; refused with
    // FL_FORMAT_UNSUPPORTED rather than silently dropped. Android external
    // formats (native YUV) allow SAMPLED only.
    VkImageUsageFlags usage;
    uint32_t flags;
    // Combined NV12 sampling. Zero-initialized means BT.709 narrow range;
    // VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY samples raw YCbCr.
    VkSamplerYcbcrModelConversion ycbcrModel;
    VkSamplerYcbcrRange ycbcrRange;
    VkFilter chromaFilter; // zero = VK_FILTER_NEAREST
} flVkImportDesc;

// One ring image as Vulkan objects, owned by the caller (flVkDestroyImage).
typedef struct flVkImage {
    uint64_t generation;      // flChannelInfo::generation at import
    uint32_t width, height;
    flFormat format;
    uint32_t imageCount;      // 1, or 2 for per-plane NV12
    VkImage image[2];
    VkImageView view[2];      // VK_NULL_HANDLE when not requested/applicable
    VkFormat viewFormat[2];   // image formats; UNDEFINED for Android external
    VkExtent2D extent[2];
    VkImageAspectFlags aspect[2]; // for barriers and copies
    VkImageUsageFlags usage;
    uint64_t externalFormat;  // Android external format, else 0
    // Combined NV12 only: the sampler that reads `view` must be created with
    // the same conversion (VkSamplerYcbcrConversionInfo); owned by this image.
    VkSamplerYcbcrConversion ycbcr;
    uint32_t memoryCount;
    VkDeviceMemory memory[4];
    // The queue family on the other side of the ring, for ownership transfers.
    uint32_t externalQueueFamily;
} flVkImage;

// Is the import still the channel's storage? Re-import when this returns 0:
// the ring was reallocated (geometry change) and the old images are gone.
FL_VK_INLINE int flVkImageCurrent(flChannel* ch, const flVkImage* image) {
    flChannelInfo info;
    if (!ch || !image || flQuery(ch, &info) != FL_OK) return 0;
    return info.generation == image->generation;
}

// Destroy after ALL GPU work using it has completed. Safe on a zeroed image.
FL_VK_INLINE void flVkDestroyImage(const flVkContext* ctx, flVkImage* image) {
    if (!ctx || !image || !ctx->device) return;
    for (uint32_t i = 0; i < 2; ++i) {
        if (image->view[i]) ctx->DestroyImageView(ctx->device, image->view[i], ctx->allocator);
        if (image->image[i]) ctx->DestroyImage(ctx->device, image->image[i], ctx->allocator);
    }
    if (image->ycbcr && ctx->DestroySamplerYcbcrConversion)
        ctx->DestroySamplerYcbcrConversion(ctx->device, image->ycbcr, ctx->allocator);
    for (uint32_t i = 0; i < image->memoryCount && i < 4; ++i)
        if (image->memory[i]) ctx->FreeMemory(ctx->device, image->memory[i], ctx->allocator);
    memset(image, 0, sizeof(*image));
}

FL_VK_INLINE VkFormatFeatureFlags flVkUsageFeatures_(VkImageUsageFlags usage) {
    VkFormatFeatureFlags f = 0;
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) f |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) f |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) f |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) f |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) f |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return f;
}

FL_VK_INLINE int flVkMemoryType_(const flVkContext* ctx, uint32_t bits) {
    VkPhysicalDeviceMemoryProperties mp;
    ctx->GetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &mp);
    int any = -1;
    for (uint32_t t = 0; t < mp.memoryTypeCount; ++t) {
        if (!(bits & (1u << t))) continue;
        if (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) return (int)t;
        if (any < 0) any = (int)t;
    }
    return any;
}

FL_VK_INLINE void flVkImageRequirements_(const flVkContext* ctx, VkImage image,
    VkImageAspectFlagBits plane, VkMemoryRequirements* req, int* dedicated) {
    VkImagePlaneMemoryRequirementsInfo planeInfo;
    memset(&planeInfo, 0, sizeof(planeInfo));
    planeInfo.sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
    planeInfo.planeAspect = plane;
    VkImageMemoryRequirementsInfo2 info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    info.pNext = plane ? &planeInfo : NULL;
    info.image = image;
    VkMemoryDedicatedRequirements ded;
    memset(&ded, 0, sizeof(ded));
    ded.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 out;
    memset(&out, 0, sizeof(out));
    out.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    out.pNext = &ded;
    ctx->GetImageMemoryRequirements2(ctx->device, &info, &out);
    *req = out.memoryRequirements;
    *dedicated = plane ? 0 : (ded.requiresDedicatedAllocation || ded.prefersDedicatedAllocation);
}

FL_VK_INLINE flResult flVkCreateViews_(const flVkContext* ctx, const flVkImportDesc* desc,
    flVkImage* img, int ycbcrExternal, const void* conversionNext,
    VkComponentMapping suggested, VkChromaLocation xOff, VkChromaLocation yOff) {
    VkImageUsageFlags viewUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((desc->flags & FL_VK_IMPORT_NO_VIEW) || !(desc->usage & viewUsage)) return FL_OK;
    int combinedYuv = img->imageCount == 1 &&
        (img->format == FL_FORMAT_NV12 || ycbcrExternal);
    if (combinedYuv) {
        if (!(desc->usage & VK_IMAGE_USAGE_SAMPLED_BIT)) return FL_OK;
        if (!ctx->CreateSamplerYcbcrConversion) return FL_FORMAT_UNSUPPORTED;
        VkSamplerYcbcrConversionCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        ci.pNext = conversionNext;
        ci.format = img->viewFormat[0];
        ci.ycbcrModel = desc->ycbcrModel == VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY
                            ? VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709 : desc->ycbcrModel;
        ci.ycbcrRange = desc->ycbcrModel == VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY
                            ? VK_SAMPLER_YCBCR_RANGE_ITU_NARROW : desc->ycbcrRange;
        ci.components = suggested;
        ci.xChromaOffset = xOff;
        ci.yChromaOffset = yOff;
        ci.chromaFilter = desc->chromaFilter;
        if (ctx->CreateSamplerYcbcrConversion(ctx->device, &ci, ctx->allocator, &img->ycbcr) != VK_SUCCESS)
            return FL_FORMAT_UNSUPPORTED;
    }
    for (uint32_t i = 0; i < img->imageCount; ++i) {
        VkSamplerYcbcrConversionInfo conv;
        memset(&conv, 0, sizeof(conv));
        conv.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
        conv.conversion = img->ycbcr;
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.pNext = img->ycbcr ? &conv : NULL;
        vi.image = img->image[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = img->viewFormat[i];
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (ctx->CreateImageView(ctx->device, &vi, ctx->allocator, &img->view[i]) != VK_SUCCESS)
            return FL_INTERNAL;
    }
    return FL_OK;
}

FL_VK_INLINE VkImageCreateInfo flVkImageInfo_(const void* next, VkFormat format,
    uint32_t w, uint32_t h, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags) {
    VkImageCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.pNext = next;
    ci.flags = flags;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent.width = w;
    ci.extent.height = h;
    ci.extent.depth = 1;
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = tiling;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return ci;
}

// Allocate (import) and bind one memory object. `importNext` is the import
// struct chain; `dedicatedImage` adds VkMemoryDedicatedAllocateInfo.
FL_VK_INLINE flResult flVkBind_(const flVkContext* ctx, flVkImage* img, VkImage image,
    VkImageAspectFlagBits plane, const void* importNext, uint32_t typeBits, VkDeviceSize size,
    int dedicated, VkDeviceSize memoryOffset) {
    if (img->memoryCount >= 4) return FL_INTERNAL;
    int type = flVkMemoryType_(ctx, typeBits);
    if (type < 0) return FL_FORMAT_UNSUPPORTED;
    VkMemoryDedicatedAllocateInfo ded;
    memset(&ded, 0, sizeof(ded));
    ded.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    ded.pNext = importNext;
    ded.image = image;
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext = dedicated ? (const void*)&ded : importNext;
    ai.allocationSize = size;
    ai.memoryTypeIndex = (uint32_t)type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (ctx->AllocateMemory(ctx->device, &ai, ctx->allocator, &memory) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    img->memory[img->memoryCount++] = memory;
    VkBindImagePlaneMemoryInfo planeBind;
    memset(&planeBind, 0, sizeof(planeBind));
    planeBind.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
    planeBind.planeAspect = plane;
    VkBindImageMemoryInfo bind;
    memset(&bind, 0, sizeof(bind));
    bind.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    bind.pNext = plane ? &planeBind : NULL;
    bind.image = image;
    bind.memory = memory;
    bind.memoryOffset = memoryOffset;
    return ctx->BindImageMemory2(ctx->device, 1, &bind) == VK_SUCCESS ? FL_OK : FL_INTERNAL;
}

#if defined(_WIN32)

FL_VK_INLINE flResult flVkImportNative_(const flVkContext* ctx, flImage* image,
    const flVkImportDesc* desc, flVkImage* img) {
    const VkExternalMemoryHandleTypeFlagBits type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    if (desc->flags & FL_VK_IMPORT_PLANES) return FL_FORMAT_UNSUPPORTED;
    // Refused, not attempted: NVIDIA imports a D3D11 NV12 texture "successfully"
    // and the chroma plane reads back as zeros (measured, RTX 5090; vkrender
    // hit the same). Silent corruption is worse than FL_FORMAT_UNSUPPORTED.
    // Sample NV12 rings through D3D11, or use an RGB ring for Vulkan.
    if (img->format == FL_FORMAT_NV12) return FL_FORMAT_UNSUPPORTED;
    HANDLE handle = (HANDLE)flImageSharedHandle(image);
    if (!handle) return FL_INVALID;
    VkFormat format = flVkFormat(img->format);
    VkPhysicalDeviceExternalImageFormatInfo ext;
    memset(&ext, 0, sizeof(ext));
    ext.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    ext.handleType = type;
    VkPhysicalDeviceImageFormatInfo2 fi;
    memset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    fi.pNext = &ext;
    fi.format = format;
    fi.type = VK_IMAGE_TYPE_2D;
    fi.tiling = VK_IMAGE_TILING_OPTIMAL;
    fi.usage = desc->usage;
    VkExternalImageFormatProperties extProps;
    memset(&extProps, 0, sizeof(extProps));
    extProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 props;
    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    props.pNext = &extProps;
    if (ctx->GetPhysicalDeviceImageFormatProperties2(ctx->physicalDevice, &fi, &props) != VK_SUCCESS ||
        !(extProps.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
        return FL_FORMAT_UNSUPPORTED;
    VkExternalMemoryImageCreateInfo extImage;
    memset(&extImage, 0, sizeof(extImage));
    extImage.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    extImage.handleTypes = type;
    VkImageCreateInfo ci = flVkImageInfo_(&extImage, format, img->width, img->height,
        VK_IMAGE_TILING_OPTIMAL, desc->usage, 0);
    img->imageCount = 1;
    img->viewFormat[0] = format;
    if (ctx->CreateImage(ctx->device, &ci, ctx->allocator, &img->image[0]) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    VkMemoryRequirements req;
    int dedicated = 0;
    flVkImageRequirements_(ctx, img->image[0], (VkImageAspectFlagBits)0, &req, &dedicated);
    VkMemoryWin32HandlePropertiesKHR hp;
    memset(&hp, 0, sizeof(hp));
    hp.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    if (ctx->GetMemoryWin32HandlePropertiesKHR(ctx->device, type, handle, &hp) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    // Importing a Win32 handle never transfers ownership; FrameLink keeps it.
    VkImportMemoryWin32HandleInfoKHR imp;
    memset(&imp, 0, sizeof(imp));
    imp.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    imp.handleType = type;
    imp.handle = handle;
    // D3D11 texture imports are dedicated-only on every desktop driver.
    return flVkBind_(ctx, img, img->image[0], (VkImageAspectFlagBits)0, &imp,
        req.memoryTypeBits & hp.memoryTypeBits, req.size, 1, 0);
}

#elif defined(__ANDROID__)

FL_VK_INLINE flResult flVkImportNative_(const flVkContext* ctx, flImage* image,
    const flVkImportDesc* desc, flVkImage* img, VkComponentMapping* components,
    VkChromaLocation* xOff, VkChromaLocation* yOff) {
    if (desc->flags & FL_VK_IMPORT_PLANES) return FL_FORMAT_UNSUPPORTED;
    struct AHardwareBuffer* ahb = flImageAHardwareBuffer(image);
    if (!ahb) return FL_INVALID;
    VkAndroidHardwareBufferFormatPropertiesANDROID fp;
    memset(&fp, 0, sizeof(fp));
    fp.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferPropertiesANDROID bp;
    memset(&bp, 0, sizeof(bp));
    bp.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    bp.pNext = &fp;
    if (ctx->GetAndroidHardwareBufferPropertiesANDROID(ctx->device, ahb, &bp) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    VkFormat format = fp.format;
    int external = format == VK_FORMAT_UNDEFINED;
    if (external && (desc->usage & ~(VkImageUsageFlags)VK_IMAGE_USAGE_SAMPLED_BIT))
        return FL_FORMAT_UNSUPPORTED;
    if (!external) {
        VkFormatFeatureFlags need = flVkUsageFeatures_(desc->usage);
        if ((fp.formatFeatures & need) != need) return FL_FORMAT_UNSUPPORTED;
    }
    *components = fp.samplerYcbcrConversionComponents;
    *xOff = fp.suggestedXChromaOffset;
    *yOff = fp.suggestedYChromaOffset;
    img->externalFormat = external ? fp.externalFormat : 0;
    VkExternalFormatANDROID ef;
    memset(&ef, 0, sizeof(ef));
    ef.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    ef.externalFormat = img->externalFormat;
    VkExternalMemoryImageCreateInfo extImage;
    memset(&extImage, 0, sizeof(extImage));
    extImage.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    extImage.pNext = &ef;
    extImage.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo ci = flVkImageInfo_(&extImage, format, img->width, img->height,
        VK_IMAGE_TILING_OPTIMAL, desc->usage, 0);
    img->imageCount = 1;
    img->viewFormat[0] = format;
    if (ctx->CreateImage(ctx->device, &ci, ctx->allocator, &img->image[0]) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    // Vulkan acquires its own AHardwareBuffer reference.
    VkImportAndroidHardwareBufferInfoANDROID imp;
    memset(&imp, 0, sizeof(imp));
    imp.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    imp.buffer = ahb;
    return flVkBind_(ctx, img, img->image[0], (VkImageAspectFlagBits)0, &imp,
        bp.memoryTypeBits, bp.allocationSize, 1, 0);
}

#else // Linux

FL_VK_INLINE int flVkModifierFeatures_(const flVkContext* ctx, VkFormat format,
    uint64_t modifier, uint32_t planes, VkFormatFeatureFlags need) {
    VkDrmFormatModifierPropertiesEXT mods[64];
    VkDrmFormatModifierPropertiesListEXT list;
    memset(&list, 0, sizeof(list));
    list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
    VkFormatProperties2 fp;
    memset(&fp, 0, sizeof(fp));
    fp.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    fp.pNext = &list;
    ctx->GetPhysicalDeviceFormatProperties2(ctx->physicalDevice, format, &fp);
    if (list.drmFormatModifierCount > 64) list.drmFormatModifierCount = 64;
    list.pDrmFormatModifierProperties = mods;
    ctx->GetPhysicalDeviceFormatProperties2(ctx->physicalDevice, format, &fp);
    for (uint32_t i = 0; i < list.drmFormatModifierCount; ++i) {
        if (mods[i].drmFormatModifier != modifier) continue;
        return mods[i].drmFormatModifierPlaneCount == planes &&
               (mods[i].drmFormatModifierTilingFeatures & need) == need;
    }
    return 0;
}

FL_VK_INLINE int flVkImportable_(const flVkContext* ctx, VkFormat format, uint64_t modifier,
    VkImageUsageFlags usage, VkImageCreateFlags flags) {
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod;
    memset(&mod, 0, sizeof(mod));
    mod.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
    mod.drmFormatModifier = modifier;
    mod.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkPhysicalDeviceExternalImageFormatInfo ext;
    memset(&ext, 0, sizeof(ext));
    ext.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    ext.pNext = &mod;
    ext.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkPhysicalDeviceImageFormatInfo2 fi;
    memset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    fi.pNext = &ext;
    fi.format = format;
    fi.type = VK_IMAGE_TYPE_2D;
    fi.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    fi.usage = usage;
    fi.flags = flags;
    VkExternalImageFormatProperties extProps;
    memset(&extProps, 0, sizeof(extProps));
    extProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 props;
    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    props.pNext = &extProps;
    return ctx->GetPhysicalDeviceImageFormatProperties2(ctx->physicalDevice, &fi, &props) == VK_SUCCESS &&
           (extProps.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
}

// Import one dma-buf fd as memory for `image` (dup'd; Vulkan owns the dup),
// bound `memoryOffset` bytes into the buffer.
FL_VK_INLINE flResult flVkBindFd_(const flVkContext* ctx, flVkImage* img, VkImage image,
    VkImageAspectFlagBits plane, int fd, VkDeviceSize memoryOffset) {
    VkMemoryRequirements req;
    int dedicated = 0;
    flVkImageRequirements_(ctx, image, plane, &req, &dedicated);
    if (memoryOffset) dedicated = 0; // a dedicated allocation binds at offset 0
    off_t end = lseek(fd, 0, SEEK_END);
    if (end <= 0 || (VkDeviceSize)end < memoryOffset + req.size) return FL_FORMAT_UNSUPPORTED;
    VkMemoryFdPropertiesKHR fp;
    memset(&fp, 0, sizeof(fp));
    fp.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    if (ctx->GetMemoryFdPropertiesKHR(ctx->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                      fd, &fp) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    int owned = dup(fd);
    if (owned < 0) return FL_INTERNAL;
    VkImportMemoryFdInfoKHR imp;
    memset(&imp, 0, sizeof(imp));
    imp.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    imp.fd = owned;
    uint32_t before = img->memoryCount;
    flResult r = flVkBind_(ctx, img, image, plane, &imp, req.memoryTypeBits & fp.memoryTypeBits,
                           (VkDeviceSize)end, dedicated, memoryOffset);
    if (img->memoryCount == before) close(owned); // allocation failed: fd still ours
    return r;
}

FL_VK_INLINE flResult flVkImportNative_(const flVkContext* ctx, flImage* image,
    const flVkImportDesc* desc, flVkImage* img) {
    flDmabuf d;
    if (flImageDmabuf(image, &d) != FL_OK || d.planes == 0 || d.planes > 4) return FL_INVALID;
    for (uint32_t p = 0; p < d.planes; ++p) if (d.fd[p] < 0) return FL_INVALID;
    // One object or several? fds are per plane but usually name one buffer.
    int disjoint = 0;
    struct stat s0;
    if (fstat(d.fd[0], &s0) != 0) return FL_INVALID;
    for (uint32_t p = 1; p < d.planes; ++p) {
        struct stat sp;
        if (fstat(d.fd[p], &sp) != 0) return FL_INVALID;
        if (sp.st_ino != s0.st_ino || sp.st_dev != s0.st_dev) disjoint = 1;
    }
    VkFormat format = flVkFormat(img->format);
    VkFormatFeatureFlags need = flVkUsageFeatures_(desc->usage);
    int nv12 = img->format == FL_FORMAT_NV12;
    int planar = nv12 && (desc->flags & FL_VK_IMPORT_PLANES);
    if (nv12 && !planar) {
        VkImageCreateFlags flags = disjoint ? VK_IMAGE_CREATE_DISJOINT_BIT : 0;
        VkFormatFeatureFlags needCombined = need | (disjoint ? VK_FORMAT_FEATURE_DISJOINT_BIT : 0);
        int combined = flVkModifierFeatures_(ctx, format, d.modifier, d.planes, needCombined) &&
                       flVkImportable_(ctx, format, d.modifier, desc->usage, flags);
        if (!combined) {
            if (desc->flags & FL_VK_IMPORT_COMBINED) return FL_FORMAT_UNSUPPORTED;
            planar = 1;
        }
    }
    if (planar) {
        if (d.planes != 2) return FL_FORMAT_UNSUPPORTED;
        const VkFormat pf[2] = {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM};
        img->imageCount = 2;
        for (uint32_t p = 0; p < 2; ++p) {
            uint32_t w = p ? img->width / 2 : img->width, h = p ? img->height / 2 : img->height;
            if (!flVkModifierFeatures_(ctx, pf[p], d.modifier, 1, need) ||
                !flVkImportable_(ctx, pf[p], d.modifier, desc->usage, 0))
                return FL_FORMAT_UNSUPPORTED;
            // The plane's offset goes in the memory bind, not the layout: the
            // NVIDIA driver ignores pPlaneLayouts[0].offset for single-plane
            // images (measured: chroma landed on luma), and the spec defines
            // the two as equivalent.
            VkSubresourceLayout layout;
            memset(&layout, 0, sizeof(layout));
            layout.rowPitch = d.stride[p];
            VkImageDrmFormatModifierExplicitCreateInfoEXT mod;
            memset(&mod, 0, sizeof(mod));
            mod.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
            mod.drmFormatModifier = d.modifier;
            mod.drmFormatModifierPlaneCount = 1;
            mod.pPlaneLayouts = &layout;
            VkExternalMemoryImageCreateInfo extImage;
            memset(&extImage, 0, sizeof(extImage));
            extImage.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            extImage.pNext = &mod;
            extImage.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkImageCreateInfo ci = flVkImageInfo_(&extImage, pf[p], w, h,
                VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, desc->usage, 0);
            img->viewFormat[p] = pf[p];
            img->extent[p].width = w;
            img->extent[p].height = h;
            img->aspect[p] = VK_IMAGE_ASPECT_COLOR_BIT;
            if (ctx->CreateImage(ctx->device, &ci, ctx->allocator, &img->image[p]) != VK_SUCCESS)
                return FL_FORMAT_UNSUPPORTED;
            VkMemoryRequirements req;
            int dedicated = 0;
            flVkImageRequirements_(ctx, img->image[p], (VkImageAspectFlagBits)0, &req, &dedicated);
            if (req.alignment && d.offset[p] % req.alignment) return FL_FORMAT_UNSUPPORTED;
            flResult r = flVkBindFd_(ctx, img, img->image[p], (VkImageAspectFlagBits)0, d.fd[p], d.offset[p]);
            if (r != FL_OK) return r;
        }
        return FL_OK;
    }
    if (!nv12 && (!flVkModifierFeatures_(ctx, format, d.modifier, d.planes, need) ||
                  !flVkImportable_(ctx, format, d.modifier, desc->usage, 0)))
        return FL_FORMAT_UNSUPPORTED;
    if (!nv12 && disjoint) return FL_FORMAT_UNSUPPORTED;
    VkSubresourceLayout layouts[4];
    memset(layouts, 0, sizeof(layouts));
    for (uint32_t p = 0; p < d.planes; ++p) {
        layouts[p].offset = d.offset[p];
        layouts[p].rowPitch = d.stride[p];
    }
    VkImageDrmFormatModifierExplicitCreateInfoEXT mod;
    memset(&mod, 0, sizeof(mod));
    mod.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    mod.drmFormatModifier = d.modifier;
    mod.drmFormatModifierPlaneCount = d.planes;
    mod.pPlaneLayouts = layouts;
    VkExternalMemoryImageCreateInfo extImage;
    memset(&extImage, 0, sizeof(extImage));
    extImage.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    extImage.pNext = &mod;
    extImage.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkImageCreateInfo ci = flVkImageInfo_(&extImage, format, img->width, img->height,
        VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, desc->usage,
        disjoint ? VK_IMAGE_CREATE_DISJOINT_BIT : 0);
    img->imageCount = 1;
    img->viewFormat[0] = format;
    if (ctx->CreateImage(ctx->device, &ci, ctx->allocator, &img->image[0]) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
    if (!disjoint)
        return flVkBindFd_(ctx, img, img->image[0], (VkImageAspectFlagBits)0, d.fd[0], 0);
    img->aspect[0] = VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT;
    for (uint32_t p = 0; p < d.planes; ++p) {
        flResult r = flVkBindFd_(ctx, img, img->image[0],
            (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT << p), d.fd[p], 0);
        if (r != FL_OK) return r;
    }
    return FL_OK;
}

#endif

// Import one ring image (flFrame::image or flBuffer::image) of `channel`.
// Zero copy: the VkImage aliases FrameLink's storage. Import each slot once per
// channel generation and cache it; FL_FORMAT_UNSUPPORTED when the driver cannot
// import this buffer for the requested usage (nothing is left allocated).
FL_VK_INLINE flResult flVkImportImage(const flVkContext* ctx, flChannel* channel,
    flImage* image, const flVkImportDesc* desc, flVkImage* out) {
    if (!out) return FL_INVALID;
    memset(out, 0, sizeof(*out));
    if (!ctx || !ctx->device || !channel || !image || !desc || !desc->usage) return FL_INVALID;
    flChannelInfo info;
    flResult r = flQuery(channel, &info);
    if (r != FL_OK) return r;
    flVkImage img;
    memset(&img, 0, sizeof(img));
    img.generation = info.generation;
    img.width = info.width;
    img.height = info.height;
    img.format = info.format;
    img.usage = desc->usage;
#if defined(_WIN32)
    img.externalQueueFamily = VK_QUEUE_FAMILY_EXTERNAL;
#else
    img.externalQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
#endif
    VkComponentMapping identity;
    memset(&identity, 0, sizeof(identity));
    VkChromaLocation xOff = VK_CHROMA_LOCATION_MIDPOINT, yOff = VK_CHROMA_LOCATION_MIDPOINT;
    const void* conversionNext = NULL;
#if defined(__ANDROID__)
    r = flVkImportNative_(ctx, image, desc, &img, &identity, &xOff, &yOff);
    VkExternalFormatANDROID ef;
    memset(&ef, 0, sizeof(ef));
    ef.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    ef.externalFormat = img.externalFormat;
    if (img.externalFormat) conversionNext = &ef;
#else
    r = flVkImportNative_(ctx, image, desc, &img);
#endif
    if (r == FL_OK && img.imageCount == 1) {
        img.extent[0].width = img.width;
        img.extent[0].height = img.height;
        if (!img.aspect[0]) img.aspect[0] = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if (r == FL_OK)
        r = flVkCreateViews_(ctx, desc, &img, img.externalFormat != 0, conversionNext,
                             identity, xOff, yOff);
    if (r != FL_OK) {
        flVkDestroyImage(ctx, &img);
        return r;
    }
    *out = img;
    return FL_OK;
}

// Queue-family ownership transfer FROM the other side of the ring, for each
// image of `image` (count returned; up to 2). Record before your first access.
// `discard`: a producer overwriting every pixel may pass 1 (oldLayout
// UNDEFINED); a consumer must pass 0.
FL_VK_INLINE uint32_t flVkAcquireBarriers(const flVkImage* image, uint32_t queueFamily,
    VkImageLayout newLayout, VkAccessFlags dstAccess, int discard, VkImageMemoryBarrier out[2]) {
    if (!image || !out) return 0;
    for (uint32_t i = 0; i < image->imageCount && i < 2; ++i) {
        VkImageMemoryBarrier* b = &out[i];
        memset(b, 0, sizeof(*b));
        b->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b->dstAccessMask = dstAccess;
        b->oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        b->newLayout = newLayout;
        b->srcQueueFamilyIndex = image->externalQueueFamily;
        b->dstQueueFamilyIndex = queueFamily;
        b->image = image->image[i];
        b->subresourceRange.aspectMask = image->aspect[i];
        b->subresourceRange.levelCount = 1;
        b->subresourceRange.layerCount = 1;
    }
    return image->imageCount;
}

// Queue-family ownership transfer back TO the other side, in GENERAL layout.
// Record after your last access, in the submit that signals FrameLink's fence.
FL_VK_INLINE uint32_t flVkReleaseBarriers(const flVkImage* image, uint32_t queueFamily,
    VkImageLayout oldLayout, VkAccessFlags srcAccess, VkImageMemoryBarrier out[2]) {
    if (!image || !out) return 0;
    for (uint32_t i = 0; i < image->imageCount && i < 2; ++i) {
        VkImageMemoryBarrier* b = &out[i];
        memset(b, 0, sizeof(*b));
        b->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b->srcAccessMask = srcAccess;
        b->oldLayout = oldLayout;
        b->newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b->srcQueueFamilyIndex = queueFamily;
        b->dstQueueFamilyIndex = image->externalQueueFamily;
        b->image = image->image[i];
        b->subresourceRange.aspectMask = image->aspect[i];
        b->subresourceRange.levelCount = 1;
        b->subresourceRange.layerCount = 1;
    }
    return image->imageCount;
}

// ---- synchronization -----------------------------------------------------------

// A semaphore operation to add to your submit: wait or signal `semaphore` at
// `value` (timeline; Windows) or as a binary semaphore (value 0; POSIX).
// semaphore == VK_NULL_HANDLE means there is nothing to add.
typedef struct flVkSemaphoreOp {
    VkSemaphore semaphore;
    uint64_t value;
} flVkSemaphoreOp;

// Per-channel Vulkan synchronization state. One per channel per device.
//
// Windows: a ring reallocation (flRequestGeometry) replaces the channel's
// shared fences too. The sync calls notice the new generation and import the
// new fences; the old semaphores may still be referenced by GPU work you have
// submitted, so they are parked (up to FL_VK_MAX_RETIRED) and destroyed by
// flVkSyncCollect or flVkDestroySync. A call returns FL_BUSY when the parking
// space is full: let that work complete, then flVkSyncCollect.
#define FL_VK_MAX_RETIRED 8
typedef struct flVkSync {
    flChannel* channel;
    int producer;
    uint32_t flags; // consumer channel flags
    uint64_t generation;  // ring generation the fences belong to
    VkSemaphore ready;    // Windows: imported shared ready fence (timeline)
    VkSemaphore consumed; // Windows: imported shared consumed fence (timeline)
    VkSemaphore retired[FL_VK_MAX_RETIRED];
    uint32_t retiredCount;
    // POSIX: binary SYNC_FD-exportable semaphore signalled by your submit and
    // exported by flVkFrameRelease/flVkEndSubmit. Reused; one signal in flight.
    VkSemaphore exportable;
    int signalPending;
    uint64_t signalValue; // Windows producer: flBeginSubmit's value
} flVkSync;

FL_VK_INLINE void flVkDestroySync(const flVkContext* ctx, flVkSync* sync);

#if defined(_WIN32)
FL_VK_INLINE flResult flVkImportTimeline_(const flVkContext* ctx, void* handle, VkSemaphore* out) {
    VkSemaphoreTypeCreateInfo type;
    memset(&type, 0, sizeof(type));
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    ci.pNext = &type;
    if (ctx->CreateSemaphore(ctx->device, &ci, ctx->allocator, out) != VK_SUCCESS) return FL_FORMAT_UNSUPPORTED;
    // D3D12_FENCE also covers fences from ID3D11Device5::CreateFence. Win32
    // handle imports never take ownership; the handle stays FrameLink's.
    VkImportSemaphoreWin32HandleInfoKHR imp;
    memset(&imp, 0, sizeof(imp));
    imp.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
    imp.semaphore = *out;
    imp.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    imp.handle = (HANDLE)handle;
    if (ctx->ImportSemaphoreWin32HandleKHR(ctx->device, &imp) != VK_SUCCESS) {
        ctx->DestroySemaphore(ctx->device, *out, ctx->allocator);
        *out = VK_NULL_HANDLE;
        return FL_FORMAT_UNSUPPORTED;
    }
    return FL_OK;
}
#endif

#if defined(_WIN32)
// Import the channel's current shared fences into s->ready / s->consumed.
FL_VK_INLINE flResult flVkImportFences_(const flVkContext* ctx, flVkSync* s) {
    void* handle = NULL;
    if (s->producer || (s->flags & FL_GPU_SYNC)) {
        if (flSharedReadyFence(s->channel, &handle) != FL_OK || !handle) return FL_INVALID;
        flResult r = flVkImportTimeline_(ctx, handle, &s->ready);
        if (r != FL_OK) return r;
    }
    if (!s->producer && (s->flags & (FL_GPU_SYNC | FL_DEFER_RELEASE))) {
        handle = NULL;
        flResult r = flSharedConsumedFence(s->channel, &handle);
        if (r == FL_OK && handle) r = flVkImportTimeline_(ctx, handle, &s->consumed);
        else r = FL_INVALID;
        if (r != FL_OK) return r;
    }
    return FL_OK;
}
#endif

// Follow a ring reallocation (Windows replaces the fences; POSIX fences are
// per frame, so there is nothing to follow).
FL_VK_INLINE flResult flVkSyncRefresh_(const flVkContext* ctx, flVkSync* s) {
#if defined(_WIN32)
    flChannelInfo info;
    flResult r = flQuery(s->channel, &info);
    if (r != FL_OK) return r;
    if (info.generation == s->generation) return FL_OK;
    uint32_t need = (s->ready ? 1u : 0u) + (s->consumed ? 1u : 0u);
    if (s->retiredCount + need > FL_VK_MAX_RETIRED) return FL_BUSY;
    if (s->ready) s->retired[s->retiredCount++] = s->ready;
    if (s->consumed) s->retired[s->retiredCount++] = s->consumed;
    s->ready = s->consumed = VK_NULL_HANDLE;
    s->generation = info.generation;
    return flVkImportFences_(ctx, s);
#else
    (void)ctx; (void)s;
    return FL_OK;
#endif
}

// Destroy semaphores parked by ring reallocations. Call only once every GPU
// submission made before the last reallocation has completed.
FL_VK_INLINE void flVkSyncCollect(const flVkContext* ctx, flVkSync* sync) {
    if (!ctx || !sync || !ctx->device) return;
    for (uint32_t i = 0; i < sync->retiredCount && i < FL_VK_MAX_RETIRED; ++i)
        if (sync->retired[i]) ctx->DestroySemaphore(ctx->device, sync->retired[i], ctx->allocator);
    memset(sync->retired, 0, sizeof(sync->retired));
    sync->retiredCount = 0;
}

// Create the channel's semaphores. Producer or consumer is detected from the
// channel. Destroy with flVkDestroySync after the channel's GPU work completes.
FL_VK_INLINE flResult flVkCreateSync(const flVkContext* ctx, flChannel* channel, flVkSync* out) {
    if (!out) return FL_INVALID;
    memset(out, 0, sizeof(*out));
    if (!ctx || !ctx->device || !channel) return FL_INVALID;
    flVkSync s;
    memset(&s, 0, sizeof(s));
    s.channel = channel;
    s.producer = flQueryConsumerFlags(channel, &s.flags) != FL_OK;
    if (s.producer) s.flags = 0;
    flChannelInfo info;
    flResult q = flQuery(channel, &info);
    if (q != FL_OK) return q;
    s.generation = info.generation;
#if defined(_WIN32)
    flResult r = flVkImportFences_(ctx, &s);
    if (r != FL_OK) { flVkDestroySync(ctx, &s); return r; }
#else
    VkExportSemaphoreCreateInfo exp;
    memset(&exp, 0, sizeof(exp));
    exp.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exp.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    ci.pNext = &exp;
    if (ctx->CreateSemaphore(ctx->device, &ci, ctx->allocator, &s.exportable) != VK_SUCCESS)
        return FL_FORMAT_UNSUPPORTED;
#endif
    *out = s;
    return FL_OK;
}

FL_VK_INLINE void flVkDestroySync(const flVkContext* ctx, flVkSync* sync) {
    if (!ctx || !sync || !ctx->device) return;
    if (sync->ready) ctx->DestroySemaphore(ctx->device, sync->ready, ctx->allocator);
    if (sync->consumed) ctx->DestroySemaphore(ctx->device, sync->consumed, ctx->allocator);
    if (sync->exportable) ctx->DestroySemaphore(ctx->device, sync->exportable, ctx->allocator);
    flVkSyncCollect(ctx, sync);
    memset(sync, 0, sizeof(*sync));
}

// Consumer: what your first submit reading `frame` must WAIT on (stage: the
// first that touches the image). FL_GPU_SYNC channels only need it; others were
// waited for inside flAcquireFrame and get an empty op.
//
// POSIX imports the frame's sync fd into `binary`, a binary semaphore YOU own
// and that has no pending operations (one per frame in flight is typical); the
// wait you submit consumes the import. Windows ignores `binary` and returns the
// shared ready timeline at the frame's value.
FL_VK_INLINE flResult flVkFrameReady(const flVkContext* ctx, flVkSync* sync,
    const flFrame* frame, VkSemaphore binary, flVkSemaphoreOp* wait) {
    if (!wait) return FL_INVALID;
    wait->semaphore = VK_NULL_HANDLE;
    wait->value = 0;
    if (!ctx || !sync || !frame || sync->producer) return FL_INVALID;
    if (!(sync->flags & FL_GPU_SYNC)) return FL_OK;
#if defined(_WIN32)
    (void)binary;
    flResult r = flVkSyncRefresh_(ctx, sync);
    if (r != FL_OK) return r;
    wait->semaphore = sync->ready;
    wait->value = frame->_readyValue;
    return FL_OK;
#else
    int fd = -1;
    flResult r = flFrameReadySyncFd(sync->channel, frame, &fd);
    if (r != FL_OK) return r;
    if (fd < 0) return FL_OK; // already complete
    if (!binary) return FL_INVALID;
    int owned = dup(fd);
    if (owned < 0) return FL_INTERNAL;
    VkImportSemaphoreFdInfoKHR imp;
    memset(&imp, 0, sizeof(imp));
    imp.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
    imp.semaphore = binary;
    imp.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
    imp.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    imp.fd = owned;
    if (ctx->ImportSemaphoreFdKHR(ctx->device, &imp) != VK_SUCCESS) {
        close(owned);
        return FL_FORMAT_UNSUPPORTED;
    }
    wait->semaphore = binary;
    return FL_OK;
#endif
}

// Consumer: what your LAST submit reading `frame` must SIGNAL. Requires
// FL_GPU_SYNC or FL_DEFER_RELEASE (FL_INVALID otherwise: see the top of this
// header). Submit it, then call flVkFrameRelease before signalling again.
FL_VK_INLINE flResult flVkFrameConsumed(const flVkContext* ctx, flVkSync* sync,
    const flFrame* frame, flVkSemaphoreOp* signal) {
    if (!signal) return FL_INVALID;
    signal->semaphore = VK_NULL_HANDLE;
    signal->value = 0;
    if (!ctx || !sync || !frame || sync->producer) return FL_INVALID;
    if (!(sync->flags & (FL_GPU_SYNC | FL_DEFER_RELEASE))) return FL_INVALID;
    if (sync->signalPending) return FL_BUSY;
#if defined(_WIN32)
    flResult r = flVkSyncRefresh_(ctx, sync);
    if (r != FL_OK) return r;
    signal->semaphore = sync->consumed;
    signal->value = frame->_readyValue;
#else
    signal->semaphore = sync->exportable;
#endif
    sync->signalPending = 1;
    return FL_OK;
}

// Consumer, after vkQueueSubmit of the flVkFrameConsumed signal: hand the
// completion to FrameLink and release the frame. The storage is reused only
// once your GPU work completes. Call with no signal pending for consumers that
// finished their GPU reads some other way (plain flRelease).
FL_VK_INLINE flResult flVkFrameRelease(const flVkContext* ctx, flVkSync* sync, const flFrame* frame) {
    if (!ctx || !sync || !frame || sync->producer) return FL_INVALID;
    if (!sync->signalPending) return flRelease(sync->channel, frame);
#if defined(_WIN32)
    sync->signalPending = 0;
    return flRelease(sync->channel, frame);
#else
    VkSemaphoreGetFdInfoKHR get;
    memset(&get, 0, sizeof(get));
    get.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    get.semaphore = sync->exportable;
    get.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -1;
    if (ctx->GetSemaphoreFdKHR(ctx->device, &get, &fd) != VK_SUCCESS) return FL_INTERNAL;
    sync->signalPending = 0; // export resets the binary semaphore
    flResult r = flReleaseSyncFd(sync->channel, frame, fd);
    if (fd >= 0) close(fd);
    return r;
#endif
}

// Producer: what the submit that finishes writing `buffer` must SIGNAL. Opens
// the channel's single submit transaction: you MUST then vkQueueSubmit the
// signal and call flVkEndSubmit (an abandoned Windows transaction keeps the
// slot reserved; there is no cancel after begin).
FL_VK_INLINE flResult flVkBeginSubmit(const flVkContext* ctx, flVkSync* sync,
    const flBuffer* buffer, flVkSemaphoreOp* signal) {
    if (!signal) return FL_INVALID;
    signal->semaphore = VK_NULL_HANDLE;
    signal->value = 0;
    if (!ctx || !sync || !buffer || !sync->producer) return FL_INVALID;
    if (sync->signalPending) return FL_BUSY;
#if defined(_WIN32)
    flResult r = flVkSyncRefresh_(ctx, sync);
    if (r != FL_OK) return r;
    uint64_t value = 0;
    r = flBeginSubmit(sync->channel, buffer, &value);
    if (r != FL_OK) return r;
    sync->signalValue = value;
    signal->semaphore = sync->ready;
    signal->value = value;
#else
    signal->semaphore = sync->exportable;
#endif
    sync->signalPending = 1;
    return FL_OK;
}

// Producer, after vkQueueSubmit of the flVkBeginSubmit signal: publish.
FL_VK_INLINE flResult flVkEndSubmit(const flVkContext* ctx, flVkSync* sync,
    const flBuffer* buffer, int64_t ptsNs) {
    if (!ctx || !sync || !buffer || !sync->producer || !sync->signalPending) return FL_INVALID;
#if defined(_WIN32)
    flResult r = flEndSubmit(sync->channel, buffer, sync->signalValue, ptsNs);
    if (r == FL_OK) sync->signalPending = 0;
    return r;
#else
    VkSemaphoreGetFdInfoKHR get;
    memset(&get, 0, sizeof(get));
    get.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    get.semaphore = sync->exportable;
    get.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -1;
    if (ctx->GetSemaphoreFdKHR(ctx->device, &get, &fd) != VK_SUCCESS) return FL_INTERNAL;
    sync->signalPending = 0;
    flResult r = flSubmitSyncFd(sync->channel, buffer, fd, ptsNs);
    if (fd >= 0) close(fd);
    return r;
#endif
}

#ifdef __cplusplus
}
#endif
