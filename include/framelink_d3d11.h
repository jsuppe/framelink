// framelink D3D11 interop (Windows). The ONLY platform-specific header a
// Windows integrator needs; the call sequence in framelink.h is unchanged.
//
// The Linux and Android equivalents (framelink_dmabuf.h, framelink_ahb.h) and
// the portable framelink_vulkan.h are not in the spike.
#pragma once
#include <d3d11.h>

#include "framelink.h"

#ifdef __cplusplus
extern "C" {
#endif

// Adopt externally allocated shared textures as an immutable consumer-side ring.
// Handles are BORROWED on input and independently duplicated on success; caller
// may close its handles/references afterwards. No texture allocation or pixel
// copy occurs. All resources must match geometry/format, be single-mip/slice,
// non-MSAA shared NT textures on the selected adapter, and be distinct objects.
// A decoder/producer may own the allocations, but once adopted ALL writers must
// obey this ring's acquire/submit/consumed fences (native references alone do not
// grant permission to reuse). Do not bind the same storage to independent rings.
// This function does not transfer arbitrary handles between processes; the
// application must first deliver authorized handles into this process.
// Geometry requests cannot resize an external ring; create a new one after safe
// retirement. Existing close/GPU completion rules still apply. On error *out=NULL.
FL_API flResult flCreateChannelFromD3D11Handles(const char* name,
    uint32_t width, uint32_t height, flFormat format, uint32_t poolSize,
    uint32_t flags, const uint8_t adapterLUID[8], void* const* handles,
    flChannel** out);

// The texture backing an acquired image. Borrowed - do NOT Release() it; it
// dies with the channel.
FL_API ID3D11Texture2D* flImageD3D11(flImage*);

// The device the channel opened the shared textures on. Render with this (or
// with your own device on the same adapter).
FL_API ID3D11Device* flChannelD3D11Device(flChannel*);

// The raw shared NT handle, for importing into D3D12, Vulkan or CUDA instead.
// Borrowed: do NOT CloseHandle() it. This is the escape hatch that stops
// ID3D11Texture2D in the ABI from locking integrators into D3D11.
FL_API void* flImageSharedHandle(flImage*);
// Retains an imported shared fence, not the caller's handle. On success the lease
// token expires; collect waits nonblocking for this reader's fence value. Device
// removal is an error, never permission to reuse the allocation.
FL_API flResult flCompleteReadLeaseD3D11Fence(flChannel*, uint64_t lease, void* sharedFence, uint64_t value);

#ifdef __cplusplus
}
#endif
