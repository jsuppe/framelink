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
