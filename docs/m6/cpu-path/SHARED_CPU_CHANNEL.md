# Explicit shared-CPU channel and leases (Linux/Android)

`framelink_shared_cpu_channel.h` exposes a separate, opt-in C API in the existing
library. `flSharedCpuChannel` and `flSharedCpuLease` are not native `flChannel` or
`flImage` objects and have no native GPU export/import/fence entry points. Do not
cast between these opaque types. Existing factories, ABI and default paths remain
unchanged. Windows implementation is not available; declarations are POSIX-only.

The consumer owns the fresh pool. `flSharedCpuOpenConsumerFd` validates the entire
pool/session/local policy before allocating, negotiates both actual endpoint
policies, creates sealed allocations and requires positive peer import acceptance.
The producer uses `flSharedCpuOpenProducerFd` to negotiate/import. Both consume the
delegated connected SEQPACKET FD on all paths and preserve output on failure.
Caller/broker authenticates the socket and validates selection against actual
session offers. Timeout 1..60000ms bounds each I/O, not whole-session duration.

## Borrow/lease behavior

- AcquireWrite reserves a specific free slot without waiting. Another borrow or
  an outstanding submitted frame makes it BUSY. Cancel releases an unsubmitted
  reservation; Submit consumes the write lease and announces completed writes.
- ReceiveRelease explicitly collects one exact release before slot reuse. Timeout
  or peer death fails the connection, not an implicit release or retryable poll.
- Receive produces an OS-read-only view, sequence and exact PTS under a read lease.
  RetainRead increments that lease's references. Only final ReleaseRead ends the
  mapping borrow and sends the matching release; no background destructor sends.
- Close disconnects immediately and drops unborrowed pool resources. Existing
  read/write leases preserve their own mappings. Submit/cancel/final release after
  close consumes the appropriate reference and returns DISCONNECTED.
- Wrong read/write lease-kind calls return INVALID without consuming the lease.
  All channel and lease operations are externally serialized. Raw mapped pointers
  must not be used after submit/cancel/final release, even if an underlying mapping
  remains cached. No promise of defending against a caller ignoring borrow rules.

Direct in-place CPU access performs no payload copy. Callers performing memcpy
must declare COPY in the setup operation mask; both peers must permit it. No hidden
conversion, latest-wins queue, clock scheduling or payload staging is introduced.
This is a typed CPU-only route, not a native `flImage` pretending to be a DMA-BUF.

## Validation

Linux 12/12 affected contract/IPC/shared-CPU tests pass. Android public channel and
C11 API tests pass; arm64 and x86_64 builds pass. Public cross-process tests exercise
BGRA and NV12, eight frames each, all logical bytes and exact PTS/sequence. A
retained read reference demonstrably delays the release packet (30ms poll observes
no packet after partial release). Reserved writer borrows and submitted slots are
BUSY; cancel allows re-borrow. Held read and write mappings survive channel close;
subsequent release/cancel reports DISCONNECTED. Wrong-kind calls preserve ownership.
FD counts return to baseline. Invalid factory arguments consume the delegated FD.

Raw results and hashes: `shared-cpu-channel/`. Still required: coordinator/broker
selection and GStreamer/A/V-bin integration, metadata/EOS/reconnect behavior and
rate/endurance qualification. No Windows CPU allocator or product-rate claim.

Public held-lease reconnect validation: see SHARED_CPU_RECONNECT.md (100 fresh
generations each Linux/Android with exact FD/named-VMA cleanup; not in-place
reconnect or A/V continuity qualification).
