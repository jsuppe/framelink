# FrameLink media roadmap

## Image/GPU transport baseline

Merged baseline: PR #1, master `1a46650`. Linux/Windows/Android format, ownership,
reconnect, explicit fence and tracing work is documented in PLATFORM_PARITY.md.
Hardware allocator/driver limitations remain limitations, not passing tests.

## Audio/video integration (authorized 2026-10-29)

1. **Implemented:** keep separate tracks and use existing GStreamer audio
   transport first; deliver a working FrameLink-video + PCM sample.
2. **Implemented:** explicit shared session clock/base and opt-in video PTS
   preservation, legacy semantics unchanged; fixed PCM caps with first-sample
   timestamps, bounded application queues, no implicit transport conversion.
3. **Linux validated:** PCM/pixel/PTS roundtrip, missing-block and increasing
   timestamp-drift detection, reset/discontinuity checks. Detailed evidence and
   limitations: AUDIO_VIDEO.md and audio-session-20261029/.
4. **Qualification pending:** Windows runtime and Android packaged integration;
   actual audio-device clock recovery, long-term hardware drift, speaker/scanout
   latency and stall/reconnect policy. Do not label logical PTS tests as these.
5. **Conditional, not automatically required:** introduce companion PCM
   shared-memory transport only after an existing-framework experiment proves
   an unmet deployment or performance need. No audio fields in `flFrame`.

The existing sample intentionally detects rather than conceals missing blocks
and drift. A production session manager, offer/answer protocol and automatic
clock recovery are separate scope from the implemented first-stage prototype.
