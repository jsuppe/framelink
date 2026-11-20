/* framelink_vulkan.h without a GPU: compiles as C99, loads nothing it was not
 * given, validates arguments, builds ownership barriers and enforces the
 * one-signal-in-flight sync rules. Hosted CI runs it on every platform. */
#define VK_NO_PROTOTYPES
#include "framelink_vulkan.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static int lookups;
static PFN_vkVoidFunction VKAPI_PTR noEntryPoints(VkInstance instance, const char* name) {
    (void)instance; (void)name;
    ++lookups;
    return NULL;
}

int main(void) {
    uint32_t n = 0;
    const char* const* names = flVkRequiredDeviceExtensions(&n);
    CHECK(names && n >= 2);
    for (uint32_t i = 0; i < n; ++i) CHECK(names[i] && names[i][0]);

    CHECK(flVkFormat(FL_FORMAT_RGBA8) == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(flVkFormat(FL_FORMAT_RGB10A2) == VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    CHECK(flVkFormat(FL_FORMAT_NV12) == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
#if defined(__ANDROID__)
    CHECK(flVkFormat(FL_FORMAT_BGRA8) == VK_FORMAT_R8G8B8A8_UNORM);
#else
    CHECK(flVkFormat(FL_FORMAT_BGRA8) == VK_FORMAT_B8G8R8A8_UNORM);
#endif
    CHECK(flVkFormat((flFormat)99) == VK_FORMAT_UNDEFINED);

    /* Context: argument checks, then a loader that has nothing. */
    VkInstance instance = (VkInstance)(uintptr_t)0x1000;
    VkPhysicalDevice phys = (VkPhysicalDevice)(uintptr_t)0x2000;
    VkDevice device = (VkDevice)(uintptr_t)0x3000;
    flVkContext ctx;
    memset(&ctx, 0x5a, sizeof(ctx));
    CHECK(flVkInitContext(NULL, instance, phys, device, noEntryPoints, NULL) == FL_INVALID);
    CHECK(flVkInitContext(&ctx, instance, phys, device, NULL, NULL) == FL_INVALID);
    CHECK(ctx.device == VK_NULL_HANDLE && ctx.CreateImage == NULL); /* zeroed on failure */
    CHECK(flVkInitContext(&ctx, VK_NULL_HANDLE, phys, device, noEntryPoints, NULL) == FL_INVALID);
    CHECK(lookups == 0);
    CHECK(flVkInitContext(&ctx, instance, phys, device, noEntryPoints, NULL) == FL_FORMAT_UNSUPPORTED);
    CHECK(lookups == 1 && ctx.device == VK_NULL_HANDLE);

    /* Imports refuse an uninitialized context and always clear the output. */
    flVkImage img;
    memset(&img, 0x5a, sizeof(img));
    flVkImportDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    CHECK(flVkImportImage(&ctx, NULL, NULL, &desc, &img) == FL_INVALID);
    CHECK(img.image[0] == VK_NULL_HANDLE && img.memoryCount == 0);
    CHECK(flVkImportImage(&ctx, NULL, NULL, &desc, NULL) == FL_INVALID);
    flVkDestroyImage(&ctx, &img); /* no device: no calls, no crash */
    CHECK(!flVkImageCurrent(NULL, &img));

    /* Ownership barriers: GENERAL across the ring, external family per platform. */
    memset(&img, 0, sizeof(img));
    img.imageCount = 2;
    img.image[0] = (VkImage)(uintptr_t)0x10;
    img.image[1] = (VkImage)(uintptr_t)0x20;
    img.aspect[0] = img.aspect[1] = VK_IMAGE_ASPECT_COLOR_BIT;
#if defined(_WIN32)
    img.externalQueueFamily = VK_QUEUE_FAMILY_EXTERNAL;
#else
    img.externalQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
#endif
    VkImageMemoryBarrier b[2];
    CHECK(flVkAcquireBarriers(&img, 3, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, 0, b) == 2);
    for (int i = 0; i < 2; ++i) {
        CHECK(b[i].sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && b[i].pNext == NULL);
        CHECK(b[i].oldLayout == VK_IMAGE_LAYOUT_GENERAL);
        CHECK(b[i].newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        CHECK(b[i].srcQueueFamilyIndex == img.externalQueueFamily && b[i].dstQueueFamilyIndex == 3);
        CHECK(b[i].image == img.image[i] && b[i].subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
        CHECK(b[i].subresourceRange.levelCount == 1 && b[i].subresourceRange.layerCount == 1);
    }
    CHECK(flVkAcquireBarriers(&img, 3, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, b) == 2);
    CHECK(b[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED); /* producer discard */
    CHECK(flVkReleaseBarriers(&img, 3, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, b) == 2);
    CHECK(b[1].newLayout == VK_IMAGE_LAYOUT_GENERAL && b[1].srcQueueFamilyIndex == 3 &&
          b[1].dstQueueFamilyIndex == img.externalQueueFamily && b[1].srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
    CHECK(flVkAcquireBarriers(NULL, 3, VK_IMAGE_LAYOUT_GENERAL, 0, 0, b) == 0);

    /* Sync rules, on hand-built state (no channel calls are reached). */
    flVkContext live;
    memset(&live, 0, sizeof(live));
    live.device = device;
    flFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame._readyValue = 7;
    flBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    flVkSync sync;
    memset(&sync, 0, sizeof(sync));
    flVkSemaphoreOp op;
    sync.flags = 0; /* plain consumer: nothing to wait on, no GPU release */
    CHECK(flVkFrameReady(&live, &sync, &frame, VK_NULL_HANDLE, &op) == FL_OK && op.semaphore == VK_NULL_HANDLE);
    CHECK(flVkFrameConsumed(&live, &sync, &frame, &op) == FL_INVALID);
    sync.flags = FL_DEFER_RELEASE;
    sync.consumed = (VkSemaphore)(uintptr_t)0x30;
    sync.exportable = (VkSemaphore)(uintptr_t)0x30;
    CHECK(flVkFrameReady(&live, &sync, &frame, VK_NULL_HANDLE, &op) == FL_OK && op.semaphore == VK_NULL_HANDLE);
#if defined(_WIN32)
    /* Windows checks the ring generation first (fences follow reallocation);
     * with no channel there is nothing to query. */
    CHECK(flVkFrameConsumed(&live, &sync, &frame, &op) == FL_INVALID && op.semaphore == VK_NULL_HANDLE);
    sync.signalPending = 1;
#else
    CHECK(flVkFrameConsumed(&live, &sync, &frame, &op) == FL_OK && op.semaphore == sync.consumed);
    CHECK(op.value == 0);
#endif
    CHECK(flVkFrameConsumed(&live, &sync, &frame, &op) == FL_BUSY && op.semaphore == VK_NULL_HANDLE);
    CHECK(flVkBeginSubmit(&live, &sync, &buffer, &op) == FL_INVALID); /* consumer */
    CHECK(flVkEndSubmit(&live, &sync, &buffer, 0) == FL_INVALID);
    sync.producer = 1;
    sync.signalPending = 0;
    CHECK(flVkFrameConsumed(&live, &sync, &frame, &op) == FL_INVALID); /* producer */
    CHECK(flVkEndSubmit(&live, &sync, &buffer, 0) == FL_INVALID);       /* nothing begun */
    CHECK(flVkCreateSync(&live, NULL, &sync) == FL_INVALID && sync.channel == NULL);
    flVkDestroySync(&ctx, &sync);

    puts("PASS: framelink_vulkan.h contract (C99, no GPU)");
    return 0;
}
