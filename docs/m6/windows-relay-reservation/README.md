# Windows three-process GstMemory reservation relay

Base `04bababf68a37d125522fc61389a58027722fe0e`; status.json pins the exact source overlays.
Windows build, explicit reservation diagnostic and **8/8 focused regressions passed (30.60 s)**.
The registered `gst_native_relay_reservation_win` has a 30-second outer timeout.

The independent source process now uses the actual FrameLinkD3DProducer-issued
GstMemory wrapper and same-device immediate context. It publishes 16 RGBA64x32
frames through the checked two-session relay, alternating both original allocations.
The existing final receiver checks every pixel, reversed-slot kernel identity,
PTS/DTS/duration/generation, 768 exact stereo PCM frames, 37ms A/V offset,
ordered GAP/barrier/END and authenticated terminal counts.

For every frame, a duplicate submit rejects. With the other allocation reserved,
acquisition rejects while the final receiver holds its native lease. After the
relay explicitly acknowledges all remote readers drained, acquisition STILL
rejects while the source GstMemory lives, including after its GstBuffer is freed.
Only the final local memory unref permits reacquisition of that same allocation.
Unused final reservations are cancelled; child exits are checked and job-contained.
The preexisting raw-native nonempty/empty and fault regressions remain passing.

This is the real reservation adapter, not a decoder pipeline or automatic-bin
relay. Synthetic source GPU upload and receiver oracle readback are explicit;
the relay does not map/copy video. No target-rate, resource-baseline, generic
foreign decoder import, or physical output acceptance is implied. No runtime C
API, symbol, struct, wire version, policy default or production code changed.
Loom briefing was attempted but unavailable (missing chromadb).

Next: clean exact-revision integration, then independent-process compatible
real decoder integration and bounded faults. D2/D3, the original Windows startup
and lifetime failures, and M6 remain open. Hold before M7; no deferred campaigns.
