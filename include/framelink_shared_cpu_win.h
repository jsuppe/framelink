#pragma once
// Additive Windows factories for the portable CPU-only channel/lease API.
// No native GPU fallback or exported shared-section handles.
#include "framelink_shared_cpu_channel.h"
#if defined(_WIN32)
#ifdef __cplusplus
extern "C" {
#endif
// Broker must authenticate/delegate this exact LOCAL, connected, OVERLAPPED,
// message-mode pipe and pinned peer process. A matching PID is not authentication.
// Consumes connectedPipe on EVERY path. Borrows/duplicates peerProcess and optional
// cancellationEvent. peerProcess needs DUP_HANDLE, QUERY_LIMITED_INFORMATION and
// SYNCHRONIZE rights. Cancellation must be a manual-reset event, signaled to cancel
// blocked I/O; all channel/lease operations remain externally serialized.
// timeoutMs 1..60000 bounds each I/O and local allocation preparation, not the
// entire factory. Explicit flags are FRAME_METADATA and/or ORDERED_EOS; zero is
// unextended mode. Both peers must agree, with no downgrade. Outputs unchanged
// on error. Consumer descriptors must include explicit Windows page rounding.
FL_API flResult flSharedCpuOpenConsumerPipe(void* connectedPipe,void* peerProcess,
    void* cancellationEvent,uint32_t timeoutMs,const flSessionConfig*,
    const flMemoryRequirements*,uint32_t operations,const flSharedCpuDescriptor*,
    size_t count,uint32_t flags,flSharedCpuChannel** out);
FL_API flResult flSharedCpuOpenProducerPipe(void* connectedPipe,void* peerProcess,
    void* cancellationEvent,uint32_t timeoutMs,const flSessionConfig*,
    const flMemoryRequirements*,uint32_t operations,uint32_t flags,
    flSharedCpuChannel** out);
#ifdef __cplusplus
}
#endif
#endif
