# Coordinated audio-master startup — partial qualification

New explicit AudioPreferredCoordinated policy/modes4 and5 uses the existing
PRIMED/GO barrier without modifying original PTS, queue depth, or common latency.
Legacy AudioPreferred and ProducerMapped defaults remain unchanged. Android's
explicit audio control now selects coordinated mode5; it is NOT a qualified
fallback. No default policy has changed.

Linux affected4/4 passed; broker suite now17 real-peer scenarios, including
coordinated audio startup,2300ms preparation delay, finite duration with startup
and idle bounds unchanged, exact video PTS/count and contiguous PCM PTS/count.
Expanded startup comparison passed: BGRA/NV12 new-policy100/100 identities,
zero ring-full; legacy negative control68/100 submitted (32 ring-full),65 seen.

Arm64 APK built. Device audio-master300-frame smoke and60-frame diagnostic both
failed after5 video frames. The latter identifies missing PTS166666666ns
(next received200000000). Sink clock stayed at zero while the foreground Surface
callback held the first sample for about203ms; this is NOT fixed by the handshake.
The startup fix therefore addresses a demonstrated portable issue but does not
qualify this Android clock policy. Callback clock waits are now bounded to the
existing2s steady-state idle limit rather than potentially waiting indefinitely.

A subsequent ProducerMapped capture reproduced the original PCM issue with
buffered diagnostics:56/300 video and28800/480000 PCM samples before the existing
playout-exhaustion guard. Device calibration progressed from1:1 to about1.625x,
then converged while admission stalled. Hardware-clock timestamps continued
advancing. This is a causal lead, not proof of a completed repair. Original
failed controls and raw logs retained. No physical-output qualification implied.
