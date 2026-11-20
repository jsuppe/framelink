#pragma once
#include <gst/gst.h>
#ifdef __ANDROID__
#include <android/hardware_buffer.h>
// Borrowed native reference, valid while the caller retains this GstMemory.
// Retain the memory until all asynchronous GPU use completes; acquiring another
// AHB reference alone does not hold the FrameLink reader lease. CPU mapping and
// copying are intentionally unsupported by this opaque native allocator.
static inline AHardwareBuffer* gst_framelink_memory_get_hardware_buffer(GstMemory* memory) {
    if(!memory || !gst_memory_is_type(memory,"FrameLinkAHardwareBuffer"))return NULL;
    return (AHardwareBuffer*)gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),
        g_quark_from_static_string("framelink-ahb"));
}
#endif

#ifdef _WIN32
// Borrowed NT allocation handle for an intact FrameLink native reader memory.
// NULL for other memory (including ordinary D3D11 allocations). Do not close,
// write through, or transfer it without broker authorization. Retain this exact
// GstMemory until downstream GPU completion: duplicating a handle does NOT hold
// the reader lease. Intended for same-allocation identity checks/composition;
// metadata and public in-process qdata are not an authentication boundary.
static inline void* gst_framelink_memory_get_d3d11_shared_handle(GstMemory* memory) {
    if(!memory || !gst_memory_is_type(memory,"D3D11Memory") ||
       !gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),g_quark_from_static_string("framelink-reader")))return NULL;
    return gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),
        g_quark_from_static_string("framelink-d3d11-reader-shared-handle"));
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <gst/allocators/gstdmabuf.h>
// Borrowed DMA-BUF fd for an intact FrameLink native reader memory, or -1 for
// other memory (including ordinary DMA-BUF allocations). Do not close, write
// through, or transfer it without broker authorization. Retain this exact
// GstMemory until downstream GPU completion: duplicating the fd does NOT hold
// the reader lease. Intended for same-allocation identity checks (compare the
// fd's inode) and composition; in-process qdata is not an authentication boundary.
static inline int gst_framelink_memory_get_dmabuf_fd(GstMemory* memory) {
    if(!memory || !gst_is_dmabuf_memory(memory) ||
       !gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),g_quark_from_static_string("framelink-reader")))return -1;
    return gst_dmabuf_memory_get_fd(memory);
}
#endif
