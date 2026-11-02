# M5a.3b — local external-pool adoption (2026-11-01)

Windows12/12, Linux12/12, Android tablet15/15 passed; no skips. Includes four
external format cases (BGRA/NV12/RGB10A2/RGBA) on each desktop, eight Android cases
(named and authorized-socket API), plus existing reservation/resource/fence/
reconnect regressions. Android arm64/x86_64 API29 builds passed; x86 runtime not
claimed. The optional Android external-pool path requires runtime API31 getId.

Each test allocates buffers independently of FrameLink, adopts native references,
rejects alias slots and malformed geometry/layout, verifies immutable dimensions,
writes via producer and verifies pixels/PTS at consumer AND the original native
allocation. Original owner's handles/references are dropped and subsequent frames
still work. Android checks stable allocation IDs across original/producer/consumer
wrappers and consumes invalid broker FDs on failure. Original-allocation readback
is explicit test verification, not a transport copy. CPU mapping may stage or
normalize; these tests do not claim CPU map is zero-copy.

Windows uses same-process independent D3D device/channel endpoints. Linux tests
independent GBM allocations with same-process channel endpoints. Android broker
cases use socketpairs in one process, not signed cross-UID app qualification.
These tests establish local native adoption/reference lifetime, not remote pool
negotiation, GPU-decoder texture-array support, delayed-reader fan-out, physical
output or throughput. Remaining reader leases keep M5a.3 open.

Reproduce with fl_external_pool_win/Linux built then CTest external_pool_[1-4].
Android: fl_external_pool_android FORMAT [broker], formats1..4. Raw logs included.
