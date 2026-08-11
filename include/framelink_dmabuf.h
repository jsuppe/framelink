// framelink dma-buf interop (Linux). The Windows equivalent is
// framelink_d3d11.h; the call sequence in framelink.h is identical on both.
#pragma once
#include <stdint.h>

#include "framelink.h"

#ifdef __cplusplus
extern "C" {
#endif

// A buffer as the kernel and the GPU see it. Import these into Vulkan
// (VK_EXT_external_memory_dma_buf), EGL (EGL_EXT_image_dma_buf_import), GBM or
// a video encoder.
//
// The fds are BORROWED - do not close them; they die with the channel. dup()
// them if you need a longer life.
typedef struct flDmabuf {
    int      fd[4];
    uint64_t offset[4];
    uint64_t stride[4];
    uint32_t planes;
    uint32_t fourcc;   // DRM_FORMAT_* - BGRA8 is DRM_FORMAT_ARGB8888
    uint64_t modifier; // DRM format modifier; DRM_FORMAT_MOD_LINEAR when mapped
} flDmabuf;

FL_API flResult flImageDmabuf(flImage*, flDmabuf* out);

// flImageMap/flImageUnmap moved to framelink.h - CPU access is not dma-buf
// specific (Android maps through AHardwareBuffer_lock).

// FL_MAP_CPU and flCreateChannelEx now live in framelink.h - the flags are not
// dma-buf specific and a Windows consumer needs them too.

#ifdef __cplusplus
}
#endif
