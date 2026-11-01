# Task 7 partial: real Linux provider resource binding

A reusable provider helper hashes the full session, ordered immutable pool,
actual kernel backing-object device/inode identities, plane offsets/strides,
native formats/modifiers and verified caller import-device identity. Producer
reserves every original pool image, inspects without mapping pixels, and cancels
all reservations on success/failure. No handle-number or pointer identity.
Unknown device/layout rejects. Output remains unchanged on failure.

The first fixture correctly failed because transport-only Linux producers have
no GPU device. The helper now requires the actual caller's verified import-device
identity; it does not fabricate that evidence or probe GPU compatibility itself.

Real external GBM allocations, original imported producer descriptors and actual
SessionProvider/GDP pipeline: BGRA and NV12 each 20/20 frames, 19,200 PCM samples,
exact timestamps. Wrong allocation and different device bindings reject with zero
media. Busy-pool inspection releases partial reservations; unknown-device rejection
and unchanged outputs checked. Four runtime cases passed in the native fixture.

This fixture permits and declares CPU copies. It proves actual pool binding and
pre-media gating, NOT native decoder throughput, no-copy output or GPU-import
compatibility. Caller must hold the exact external pool immutable until session
termination, and independently establish its endpoint import/device capabilities.
Windows/Android provider-binding adapters remain separate unfinished work.
