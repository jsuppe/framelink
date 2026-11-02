# Synthetic producer catch-up repair

The original Windows39/40 event had no identity trace, so its exact historical cause cannot be proven retrospectively. Controlled reproduction demonstrates a matching loss mechanism on both hosts: delay the synthetic producer at frame20 by55ms; overdue frames then leave appsrc back-to-back. All40 frames reach native submission and IPC, but pending replacement loses1–2 before acquisition. PCM remains complete and presentation-late/overflow counters remain0. The original max-push metric measured work, not wake lateness, so it did not expose this condition.

Before repair: Linux injected cases received39/38/38, Windows38/38/38. All source/IPC identities accounted; missingPTS400/420ms are listed in boundary-accounting.json. No recorder loss. No-stall controls received40.

Repair: the sample recovers toward its original absolute epoch at at most2x the configured source rate, with no additional wait on nominal on-time frames. Both tracks retain original PTS and PCM sample positions. No image pool, relay reservoir, FrameLink replacement policy or receiver latency is changed. The producer-pacing summary now includes maximum wake lag and catch-up hold count. FRAMELINK_TEST_PRODUCER_STALL_MS is a bounded test-only fault hook (max1000ms and within peer timeout).

After repair: all three injected cases on Linux and Windows received40/40 plus38400PCM sample-frames, zero source gaps and zero late video. No-stall controls have catchup_holds0. The permanent m5_startup regression includes the55ms stall and verifies40 actual submissions,40 receipts, all original PTS/durations and no source-gap messages.

Affected tests passed Linux4/4 and Windows4/4: startup, source drift, recovery and consumer draining. Signed Android nativeRGBA regression passed6/6: initial, producer death/restart, background/reopen and audible; every completed media run300/300 frames plus480000PCM sample-frames. Arm64 signed APK and x86_64 JNI builds passed. These are accepted Surface posts, not physical scanout/acoustic measurement.

This repairs the demonstrated synthetic-producer burst mechanism. It does not make the generic latest-frame transport lossless under arbitrary consumer/GPU/OS stalls or re-pace externally supplied GStreamer pipelines. Long stalls beyond output/queue budgets remain explicit failures/losses, not guaranteed recovery. The original uncaptured failure remains retained as evidence, not retroactively assigned an exact frame/cause.
