# Linux audio/video first-stage evidence (2026-10-29)

Host melchior, Linux, NVIDIA RTX3090 FrameLink GBM CPU-map path; Release build.
GStreamer 1.20+ required (host details via gst-inspect below).

- Full existing + new suite: 29/29 passed (`linux-suite.txt`).
- Normal: 150 video + 150 audio blocks; exact PTS and running-time segment
  mapping, representative video pixel and all PCM samples verified. No missing
  blocks/drift detected. App pull-skew p50 0.072 ms, p99 0.073 ms; video scheduled
  lateness p50 0.200 ms, p99 0.243 ms. Three-second local controlled test only.
- Faults: 150 video / 149 audio; one deliberately omitted block detected;
  40 increasing audio timestamp offsets detected; one media reset detected and
  DISCONT preserved. No claim of fault repair/resampling.
- An initial implementation left GstBaseSrc do-timestamp enabled despite
  supplying PTS; this rebased the outgoing segment, producing ~14 ms skew even
  with byte-identical PTS. Corrected by disabling automatic timestamping only
  in opt-in mode. Tests now assert segment-to-running-time identity as well as
  raw PTS. Legacy default remains automatic arrival stamping.
- No speakers/window used in these measurements. Actual presentation latency,
  long-duration hardware audio-clock drift and power/suspend were not measured.

Run instructions and exact contract: ../AUDIO_VIDEO.md.

## Android native qualification

Static SDK: `/home/melchior/dev/gstreamer-android/{arm64,x86_64}`, NDK28.2,
API29 Release. Both ABIs build. Arm64 executable run as adb shell in isolated
`/data/local/tmp/fl-av-session`; no Activity launch/replacement. GDP/TCP/core/app
plugins registered statically with explicit gio/gstreamer-net dependencies.

- Pi: 150 normal A/V pairs; skew p50/p99 0.045/0.063 ms; scheduled video lateness
  0.119/0.269 ms. Fault test detects one missing block,40 drift blocks,one reset.
- Mali tablet: 150 normal A/V pairs; skew p50/p99 0.132/0.405 ms; scheduled video
  lateness12.173/42.990 ms. Correct data/timeline but **not all frames on time**.
  Fault test detects one missing block,40 drift blocks,one reset.
- These are native trusted-shell tests, not APK deployment, audio-device-clock
  recovery or cross-app sandbox validation. x86_64 runtime not exercised here.
