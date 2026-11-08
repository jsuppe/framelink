#pragma once
// Explicit CPU-only channel extension for Linux/Android. Separate opaque types
// cannot be passed to native flChannel/flImage GPU APIs. No implicit fallback.
#include "framelink_shared_cpu.h"
#include "framelink_session.h"
#if !defined(_WIN32)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct flSharedCpuChannel flSharedCpuChannel;
typedef struct flSharedCpuLease flSharedCpuLease;
// Externally serialize ALL calls sharing a channel or its leases. The broker
// authenticates/delegates the connected SOCK_SEQPACKET socket. Both endpoints
// supply the same validated session selection and their actual memory policy.
// Consumes fd on every path. timeoutMs 1..60000 bounds each socket operation.
// Open consumer allocates desc[count] (2..16 slots); producer imports that pool.
// Operations currently 0 or COPY; callers declare all boundary payload copies.
// Outputs unchanged on error. No native handles, GPU fences or conversions.
FL_API flResult flSharedCpuOpenConsumerFd(int fd,uint32_t timeoutMs,
    const flSessionConfig*,const flMemoryRequirements*,uint32_t operations,
    const flSharedCpuDescriptor* desc,size_t count,flSharedCpuChannel** out);
FL_API flResult flSharedCpuOpenProducerFd(int fd,uint32_t timeoutMs,
    const flSessionConfig*,const flMemoryRequirements*,uint32_t operations,
    flSharedCpuChannel** out);
// Strict explicit metadata mode: BOTH peers must select the same flag. No
// downgrade to unextended peers. Original factories select flags=0 unchanged.
#define FL_SHARED_CPU_FRAME_METADATA 1u
#define FL_SHARED_CPU_ORDERED_EOS 2u
FL_API flResult flSharedCpuOpenConsumerFdEx(int fd,uint32_t timeoutMs,
    const flSessionConfig*,const flMemoryRequirements*,uint32_t operations,
    const flSharedCpuDescriptor* desc,size_t count,uint32_t flags,flSharedCpuChannel** out);
FL_API flResult flSharedCpuOpenProducerFdEx(int fd,uint32_t timeoutMs,
    const flSessionConfig*,const flMemoryRequirements*,uint32_t operations,
    uint32_t flags,flSharedCpuChannel** out);
// Metadata mode requires this submit API, and preserves DTS/duration/discontinuity
// and independent nonzero media generation. Same write-lease consumption rules.
FL_API flResult flSharedCpuSubmitWithMetadata(flSharedCpuLease*,uint64_t sequence,
    int64_t ptsNs,const flFrameMetadata*);
// Read lease only. Legacy frames return an all-zero metadata struct. Does not
// consume a reference; valid while the lease survives, including after close.
FL_API flResult flSharedCpuReadMetadata(const flSharedCpuLease*,flFrameMetadata*);
// Read the negotiated local role/policy and immutable session. All outputs
// required; unchanged on failure. Operations are actual declared copies, not
// merely allowed permissions. No borrowed internal pointers are returned.
FL_API flResult flSharedCpuQuery(flSharedCpuChannel*,flSessionConfig*,
    uint32_t* operations,uint32_t* flags,int* producer,uint32_t* slots);
// Close disconnects immediately; existing leases retain their mapped storage.
// After close, submit/release consumes the lease but reports DISCONNECTED.
FL_API void flSharedCpuClose(flSharedCpuChannel*);
// Producer: borrow a specific free slot, or BUSY. No implicit wait/recycling.
// View valid until submit/cancel; writes must finish before submit. Do not retain
// the pointer beyond the lease. Submit consumes a write lease on success/failure.
FL_API flResult flSharedCpuAcquireWrite(flSharedCpuChannel*,uint32_t slot,
    flSharedCpuLease** out,flMappedImage* view);
FL_API flResult flSharedCpuSubmit(flSharedCpuLease*,uint64_t sequence,int64_t ptsNs);
FL_API flResult flSharedCpuCancelWrite(flSharedCpuLease*);
// Non-consuming readiness wait (0..60000ms). TIMEOUT leaves the channel live;
// EOF/error readiness may return OK and must be resolved by Receive/Release.
// Externally serialized like other calls; does not recycle slots or read packets.
FL_API flResult flSharedCpuWaitReadable(flSharedCpuChannel*,uint32_t timeoutMs);
// Receive exactly one matching release to make its slot writable again.
// Timeout/peer death fails this connection; does NOT infer release of any slot.
FL_API flResult flSharedCpuReceiveRelease(flSharedCpuChannel*);
// Consumer: view is OS read-only, valid until final release. Source sequence and
// nonnegative PTS are preserved. No hidden scheduling, latest-wins or CPU copy.
FL_API flResult flSharedCpuReceive(flSharedCpuChannel*,flSharedCpuLease** out,
    flMappedImage* view,uint64_t* sequence,int64_t* ptsNs);
// EOS is independently opt-in on BOTH factories. Send after final submit;
// reserved writes must first be submitted/cancelled. No more writes after EOS.
// Outstanding reads remain valid and releases must still be drained normally.
FL_API flResult flSharedCpuSendEos(flSharedCpuChannel*);
// Required receiver for ORDERED_EOS mode. On EOS returns OK and *eos=1,
// leaving frame outputs unchanged; repeated calls return the same terminal event.
// Socket death/timeout is never EOS. On a frame returns OK and *eos=0.
FL_API flResult flSharedCpuReceiveEvent(flSharedCpuChannel*,flSharedCpuLease** out,
    flMappedImage* view,uint64_t* sequence,int64_t* ptsNs,int* eos);
// Only read leases can be retained. Each successful retain needs one release.
// Final release finishes all reads BEFORE notifying the producer. No destructor
// thread or automatic callback performs network work. Wrong lease kind rejects
// without consuming it. Raw pointer discipline remains the caller's obligation.
FL_API flResult flSharedCpuRetainRead(flSharedCpuLease*);
FL_API flResult flSharedCpuReleaseRead(flSharedCpuLease*);
#ifdef __cplusplus
}
#endif
#endif
