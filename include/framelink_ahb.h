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

// Adopt borrowed external AHBs, retaining independent references without copying
// pixels. Immutable matching geometry/native format, distinct allocation IDs and
// requested CPU/render usage are required. This opt-in path requires runtime
// AHardwareBuffer_getId (API31+); legacy allocation remains API29-compatible.
// Original owners must obey ring acquire/submit/consumed synchronization and must
// not bind the same allocation into independent writable rings.
FL_API flResult flCreateChannelFromHardwareBuffers(const char* name,
    uint32_t width, uint32_t height, flFormat format, uint32_t poolSize,
    uint32_t flags, AHardwareBuffer* const* images, flChannel** out);
// Same adoption using an authorized broker socket; consumes connectedFd on ALL
// paths, just like flCreateChannelFromSocketFd. AHB input refs remain borrowed.
FL_API flResult flCreateChannelFromSocketFdWithHardwareBuffers(int connectedFd,
    uint32_t width, uint32_t height, flFormat format, uint32_t poolSize,
    uint32_t flags, uint32_t ioTimeoutMs, AHardwareBuffer* const* images,
    flChannel** out);

// Owned, immutable consumer pool export; the counterpart of Windows'
// flSnapshotD3D11ConsumerPool and Linux's flSnapshotDmabufConsumerPool. No
// image acquisition, frame lease, copying or writer permission is implied.
// Each slot holds its own reference (AHardwareBuffer_acquire) to the exact
// original allocation. While a snapshot exists, geometry replacement is
// refused (the current geometry is returned) and a newly attaching producer
// receives FL_BUSY: the source pool/epoch is pinned. Existing media and read
// leases continue normally. Retain the snapshot for the relay graph lifetime
// to prohibit mid-graph pool changes; relay code must also preserve real
// upstream read leases until downstream readers finish. Calls on the source
// must be serialized against flClose as for other APIs. Snapshots can outlive
// source close: their buffers stay valid, but source close does not imply GPU
// completion or make old-generation frames reusable.
// flAHardwareBufferPoolSnapshotBuffer returns a BORROWED reference, valid
// until snapshot release (NULL for an invalid slot). Callers may acquire their
// own reference, but such references no longer pin the source pool.
// Authorized cross-process transfer is the caller's responsibility.
// On failure *out=NULL; producer channels are rejected with FL_INVALID.
typedef struct flAHardwareBufferPoolSnapshot flAHardwareBufferPoolSnapshot;
FL_API flResult flSnapshotAHardwareBufferConsumerPool(flChannel*, flAHardwareBufferPoolSnapshot** out);
FL_API flResult flQueryAHardwareBufferPoolSnapshot(const flAHardwareBufferPoolSnapshot*, flChannelInfo* out);
FL_API AHardwareBuffer* flAHardwareBufferPoolSnapshotBuffer(const flAHardwareBufferPoolSnapshot*, uint32_t slot);
FL_API void flReleaseAHardwareBufferPoolSnapshot(flAHardwareBufferPoolSnapshot*);

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
