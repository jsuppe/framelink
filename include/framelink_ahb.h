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

// Android broker-owned native video transport. Each call CONSUMES connectedFd
// on success and failure (use ParcelFileDescriptor.detachFd(), never getFd()).
// It must be an authorized, connected AF_UNIX/SOCK_SEQPACKET endpoint, distinct
// from the A/V control and GDP STREAM endpoints. No named socket is created.
// ioTimeoutMs is 1..60000 and bounds each handshake/control/native-handle I/O.
// Broker authentication/permissions and fresh lease identity are caller policy;
// possession of this descriptor grants access to the ring. Retain a separate
// duplicate and shutdown it to revoke; closing a duplicate alone is not revoke.
//
// Consumer allocates and starts its handshake asynchronously. Setup/peer failure
// subsequently yields FL_DISCONNECTED from flAcquireFrame. Producer open waits
// for the handshake. One connection per channel; reconnect creates a fresh
// channel/lease. Retain acquired AHB refs and wait for outstanding GPU reads
// before closing old imports; a new channel never retires the old one's work.
// On any failure *out is null (when out is nonnull).
FL_API flResult flCreateChannelFromSocketFd(int connectedFd, uint32_t width,
    uint32_t height, flFormat format, uint32_t poolSize, uint32_t flags,
    uint32_t ioTimeoutMs, flChannel** out);
FL_API flResult flOpenProducerFromSocketFd(int connectedFd, uint32_t ioTimeoutMs,
    flChannel** out);

#ifdef __cplusplus
}
#endif
