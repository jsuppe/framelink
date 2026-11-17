# Explicit shared-CPU A/V bin selection

framelinkavsink and framelinkavsrc now expose shared-cpu=false by default,
mutable only in NULL like other bin configuration. true selects the existing
explicit CPU child path before children start, with installed coordinator contexts
and broker-bound sockets. Native adopt-channel is refused in this mode.
Audio-only selection, session= discovery, and source native-memory combinations
are rejected before startup. This deliberately does not claim CPU discovery.

The six real-stream cases from SHARED_CPU_AV.md now run both through individual
elements and through both convenience bins: BGRA/NV12 exact logical pixels and
PCM bytes, common mapping preserves37ms offset, both EOS submission orders,
empty streams, conflicting terminal accounting and PCM death. Five configuration
negatives cover both bins. Aggregate EOS waits for both tracks; teardown<1s.
Linux6/6 affected tests, Android6 runtime binaries and both ABI builds pass.
Existing native/local-discovery bin regression passes on Linux and Android.
Evidence commands/logs/source hashes in shared-cpu-bins/.

No native default, discovery, overload policy, playback latency or allocation
policy changed. Windows CPU support remains unavailable. Next: authenticated
CPU rendezvous and final rate/lifetime qualification; M6 remains open and M7
maintenance hold remains.
