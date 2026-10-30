# M4 playback requirements and qualification gates

User authorization: 2026-10-30; M4 only. M1–M3 are merged, M5–M7 paused.

## Non-negotiable lip-sync contract
- Preserve incoming PTS through the sender TIME segment into session running time.
- One explicit mapping puts both audio and video on the receiver playback timeline.
- Never independently anchor tracks to their first arrival; arrival jitter is not media time.
- Audio playback clock is preferred master when a real supported audio sink supplies it.
  Any system-clock/virtual-output fallback must be visible in capabilities and telemetry.
- Actual audio sink and renderer scheduler must honor mapped due times. Transporting
  matching timestamps alone is not qualification. GPU fences are not presentation times.
- Account for pipeline/device latency once, not once per stage. Distinguish render
  submission, estimated scanout and measured presentation.
- Drift recovery must use a demonstrated clock-slaving/resampling mechanism; merely
  adding audioresample is not evidence of compensation.
- On discontinuity or clock loss, invalidate stale mappings and coordinate both tracks.
- Preserve native GPU ownership for renderer FrameLink input; existing Gst source CPU
  readback is not silently substituted or described as zero-copy.
- M3 per-pair verification ACK remains a correctness fixture, not production pacing.

## Deliverables
1. Explicit playback/low-latency modes and bounded timing policy.
2. Reusable authenticated A/V session receiver and native renderer input connection.
3. Shared-clock scheduling, bounded early hold/late drop, GPU-safe frame retirement.
4. Explicit audio underrun/overrun/discontinuity and clock-drift recovery policy.
5. Selected audio source/mute controls (one active track initially, no general mixer).
6. Queue, drop, underrun, drift, estimated offset, clock and generation diagnostics.
7. Independent fault tests, platform evidence, and physical measurement procedure/results.
8. Reviewed changes, final CI and merge with documented capability limits.

## Required validation
Nonzero segments/epochs; early/late video; +/- drift; independent track stalls;
pause/resume; generation reset; source selection/mute; audio clock/device loss;
finite queues/resource ownership; preservation of legacy video-only behavior.
Compare software due time and observed sink/render scheduling without relabeling
CPU callback time as physical presentation. Test faults must recover or explicitly
fail/re-anchor as specified, not merely produce a detection counter.

Physical flash/click validation requires an observable display and speaker plus
synchronized optical/acoustic capture. Current Linux host has only auto_null audio
and no /dev/video device. This is an equipment qualification gap, not permission to
claim physical lip sync from virtual-sink tests. Investigate other available targets
and report limitations transparently. Performance budgets must be stated and measured.
