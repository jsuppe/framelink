#pragma once
// Coordinator preflight only. Does not authenticate a peer, import a handle,
// advertise a transport, or publish a live pool. Keep out of the public ABI until
// the channel's explicit extension handshake and lease ownership are integrated.
#include "framelink_shared_cpu.h"
#include "framelink_session.h"
namespace fl {
inline flMemoryResult validateSharedCpuPool(const flSessionConfig* session,
    const flSharedCpuDescriptor* images, size_t count,
    const flMemoryRequirements* endpoints, const uint32_t* operations,
    flMemoryDecision* decisions) {
    if (!session || !images || !endpoints || !operations || !decisions ||
        count < 2 || count > FL_MAX_POOL_SIZE) return FL_MEMORY_INVALID;
    const auto sr = flSessionValidate(session);
    if (sr == FL_SESSION_VERSION_ERROR) return FL_MEMORY_VERSION_MISMATCH;
    if (sr != FL_SESSION_OK) return FL_MEMORY_INVALID;
    if (!session->videoTrackId) return FL_MEMORY_INCOMPATIBLE;
    // One reading and one writing slot besides a multi-frame unread queue.
    if (session->videoQueueFrames > 1 && session->videoQueueFrames + 2 > count)
        return FL_MEMORY_INCOMPATIBLE;
    uint64_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& d = images[i];
        const auto r = flSharedCpuDescriptorValidate(&d);
        if (r != FL_MEMORY_OK) return r;
        if (d.generation != session->generation || d.format != session->video.format ||
            d.width != session->video.width || d.height != session->video.height)
            return FL_MEMORY_INCOMPATIBLE;
        for (size_t j = 0; j < i; ++j)
            if (d.allocationId == images[j].allocationId) return FL_MEMORY_INVALID;
        // Bound the entire pool, not just each untrusted allocation.
        if (d.allocationBytes > FL_SHARED_CPU_MAX_BYTES - bytes) return FL_MEMORY_INVALID;
        bytes += d.allocationBytes;
    }
    flMemoryPath paths[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        // CPU-only exact-format boundary: no implicit upload, readback,
        // conversion or native GPU interop. COPY must still be declared when used.
        if (operations[i] & ~FL_MEMORY_COPY) return FL_MEMORY_INVALID;
        paths[i].version = FL_MEMORY_VERSION;
        paths[i].resourceType = FL_MEMORY_SHARED_CPU;
        paths[i].supportedUsage = i ? FL_MEMORY_CPU_READ : FL_MEMORY_CPU_WRITE;
        paths[i].operations = operations[i];
    }
    // Only policy OK/INCOMPATIBLE publishes diagnostics; all preflight failures
    // above preserve the caller output. OS identity/rights remain import duties.
    return flMemoryEvaluateBoundary(endpoints, paths, 2, decisions);
}
}
