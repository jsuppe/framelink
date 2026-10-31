# Expanded bounded transport pool (wire version 6)

FrameLink now exposes `FL_MAX_POOL_SIZE = 16`. Allocation remains explicit; the
sample and GStreamer source defaults remain four images. The Windows, Linux and
Android backends derive their validation limits and fixed RingDesc arrays from
this one constant. No FIFO, timestamp, latency or latest-wins policy is changed.

**Upgrade both endpoints and every embedded copy together.** The fixed RingDesc
wire layout grew, so `FL_VERSION` is 6 (previously 5). Mismatched Hello versions
are rejected before ring transfer/handle duplication; no mixed-version fallback
or reinterpretation of the old arrays is supported.

This provides capacity for applications that hold scheduled images plus current
and in-flight GPU images. Applications must budget those ownership states and
memory explicitly. A larger finite pool does not guarantee zero loss under
arbitrary stalls/bursts and does not qualify physical presentation or lip sync.

POSIX IPC now accommodates 16 images with four exported planes each (64 FDs).
Truncated ancillary data is rejected and all installed descriptors are closed.
Both IPC implementations guard the fixed 64 KiB message buffer against oversized
payloads, including integer-overflow inputs.

## Regression coverage

- `expanded_pool`: BGRA and NV12 allocate 16 images, reserve every distinct slot,
  confirm a seventeenth acquisition times out, then recycle slots 15, 8 and 0
  while every other slot remains reserved. Allocation size 17 is rejected.
- The same test checks both old-producer/new-consumer and new-producer/old-consumer
  version rejection before any ring resources are transferred.
- `ipc_expanded_capacity` (POSIX): transfers the full 64 descriptors with the
  expanded RingDesc, checks close-on-exec, rejects oversized send arguments and
  receives an intentionally truncated 65-descriptor message without leaking FDs.

Linux full regression passed **57/57** (94.16 seconds), including both new tests;
see `linux-ctest.log`. The Android arm64-v8a library and both
new tests cross-compiled successfully with NDK 28.2/API 29; device execution is
not implied. Windows must run `expanded_pool` on its supported adapter, and
Android still needs its device allocator coverage.
