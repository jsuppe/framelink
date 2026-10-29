// framelink AHardwareBuffer interop (Android). The Windows equivalent is
// framelink_d3d11.h and the Linux one framelink_dmabuf.h; the call sequence in
// framelink.h is identical everywhere.
#pragma once
#include <stdint.h>

#include "framelink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AHardwareBuffer AHardwareBuffer;

// The buffer as Android sees it. Import into Vulkan with
// VK_ANDROID_external_memory_android_hardware_buffer, or wrap for any API that
// speaks AHardwareBuffer.
//
// Describe/query the AHB's native format for GPU import: BGRA channels use RGBA
// native storage, NV12 channels use external-format YUV420. CPU mapping performs
// normalization; the native handle is not a promise of NV12 byte layout.
//
// BORROWED - do not release it; it dies with the channel. Acquire a reference
// (AHardwareBuffer_acquire) if you need a longer life.
FL_API AHardwareBuffer* flImageAHardwareBuffer(flImage*);

#ifdef __cplusplus
}
#endif
