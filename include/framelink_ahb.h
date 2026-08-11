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
// BORROWED - do not release it; it dies with the channel. Acquire a reference
// (AHardwareBuffer_acquire) if you need a longer life.
FL_API AHardwareBuffer* flImageAHardwareBuffer(flImage*);

#ifdef __cplusplus
}
#endif
