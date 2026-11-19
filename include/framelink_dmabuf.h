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

// Adopt externally allocated DMA-BUF images; inputs are borrowed and duplicated.
// No pixel allocation/copy. All slots must have compatible native layouts and
// distinct backing objects (suballocations sharing an FD across slots rejected).
// Import compatibility is checked through GBM; immutable geometry thereafter.
// Original owners must obey ring reservations/fences, not independently reuse
// storage. Authorized cross-process FD delivery is the caller's responsibility.
FL_API flResult flCreateChannelFromDmabufs(const char* name, uint32_t width,
    uint32_t height, flFormat format, uint32_t poolSize, uint32_t flags,
    const flDmabuf* images, flChannel** out);

FL_API flResult flImageDmabuf(flImage*, flDmabuf* out);

// Owned, immutable consumer pool export; the counterpart of Windows'
// flSnapshotD3D11ConsumerPool. No image acquisition, frame lease, copying or
// writer permission is implied. Each slot holds independent duplicates of the
// exact original allocation's fds. While a snapshot exists, geometry
// replacement is refused (the current geometry is returned) and a newly
// attaching producer receives FL_BUSY: the source pool/epoch is pinned.
// Existing media and read leases continue normally. Retain the snapshot for
// the relay graph lifetime to prohibit mid-graph pool changes; relay code must
// also preserve real upstream read leases until downstream readers finish.
// Calls on the source must be serialized against flClose as for other APIs.
// Snapshots can outlive source close: their fds stay valid, but source close
// does not imply GPU completion or make old-generation frames reusable.
// flDmabufPoolSnapshotImage returns BORROWED fds, valid until snapshot release.
// Callers may dup() them, but such duplicates no longer pin the source pool.
// Authorized cross-process transfer is the caller's responsibility.
// On failure *out=NULL; producer channels are rejected with FL_INVALID.
typedef struct flDmabufPoolSnapshot flDmabufPoolSnapshot;
FL_API flResult flSnapshotDmabufConsumerPool(flChannel*, flDmabufPoolSnapshot** out);
FL_API flResult flQueryDmabufPoolSnapshot(const flDmabufPoolSnapshot*, flChannelInfo* out);
FL_API flResult flDmabufPoolSnapshotImage(const flDmabufPoolSnapshot*, uint32_t slot, flDmabuf* out);
FL_API void flReleaseDmabufPoolSnapshot(flDmabufPoolSnapshot*);

// flImageMap/flImageUnmap moved to framelink.h - CPU access is not dma-buf
// specific (Android maps through AHardwareBuffer_lock).

// FL_MAP_CPU and flCreateChannelEx now live in framelink.h - the flags are not
// dma-buf specific and a Windows consumer needs them too.

#ifdef __cplusplus
}
#endif
