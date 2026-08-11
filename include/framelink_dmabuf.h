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

// A CPU pointer to the buffer, for consumers that must read pixels - writing to
// a v4l2loopback node, for instance, or encoding on the CPU.
//
// Only valid when the channel was created with FL_MAP_CPU; otherwise
// FL_FORMAT_UNSUPPORTED, because a tiled or device-local buffer cannot be
// meaningfully addressed byte-wise.
FL_API flResult flImageMap(flImage*, void** pixels, uint64_t* stride);
FL_API void     flImageUnmap(flImage*);

// FL_MAP_CPU and flCreateChannelEx now live in framelink.h - the flags are not
// dma-buf specific and a Windows consumer needs them too.

#ifdef __cplusplus
}
#endif
