#pragma once
// Explicit shared-CPU descriptor extension. This is NOT a DMA-BUF descriptor,
// an allocator, a synchronization primitive, or evidence of GPU importability.
#include "framelink_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FL_SHARED_CPU_VERSION 1u
#define FL_SHARED_CPU_WIRE_SIZE 128u
#define FL_SHARED_CPU_MAX_BYTES UINT64_C(1073741824)

typedef struct flSharedCpuDescriptor {
    uint32_t version;
    uint32_t resourceType; // Exactly FL_MEMORY_SHARED_CPU.
    uint32_t format;       // BGRA8/RGBA8/RGB10A2 packed, or exact NV12 planes.
    uint32_t width, height;
    uint32_t planeCount;
    uint32_t flags;        // Zero in v1; no implied GPU/cache/fence flags.
    uint32_t reserved32;   // Zero.
    uint64_t generation;  // Nonzero; must match the separately agreed pool epoch.
    uint64_t allocationId;// Nonzero; identity within that epoch, NOT an OS handle.
    uint64_t allocationBytes;
    uint64_t planeOffset[4];
    uint64_t planeStride[4];
    uint64_t reserved64;   // Zero.
} flSharedCpuDescriptor;

// Validate bounded, ordered, disjoint full-stride plane spans. Dimensions are
// <=8192; allocation <=1GiB. Packed offsets/strides are 4-byte aligned; NV12 is
// even-sized with 2-byte alignment. Unused plane entries must be zero.
// No handle is opened, memory mapped or permissions negotiated by these calls.
FL_API flMemoryResult flSharedCpuDescriptorValidate(const flSharedCpuDescriptor*);
// Exact-size, tagged little-endian wire message (FCS1). Unknown versions,
// resource types, flags and reserved bytes reject. Outputs unchanged on error;
// overlap between input and output is permitted. Never append this to legacy
// channel/session messages; select this extension explicitly before exchange.
FL_API flMemoryResult flSharedCpuDescriptorEncode(const flSharedCpuDescriptor*,void*,size_t);
FL_API flMemoryResult flSharedCpuDescriptorDecode(const void*,size_t,flSharedCpuDescriptor*);
// Before using media, an importer must independently verify handle size/type,
// stable-size ownership, access rights, descriptor identity/generation and the
// live lease. Both endpoints must permit their actual COPY operations through
// flMemoryEvaluateBoundary. CPU storage never advertises GPU_SAMPLE/RENDER or
// DECODER_OUTPUT solely because its bytes represent a video image.
#ifdef __cplusplus
}
#endif
