# M5a.14 — audio memory and copy-cost policy

Decision (2026-11-01): retain the existing authenticated GDP/GIO PCM transport.
Native video sharing and audio transport are separate negotiated track capabilities.
This decision does not introduce shared-memory PCM or label a whole A/V session
zero-copy. M5b exposes the existing audio path through elements/session bins.

## Why retain it now

The supported native-boundary workload achieved1080p60 with complete simultaneous
PCM and unchanged relative media timing on Linux, Windows and Android. The
[sustained task13 evidence](m5a/multihop-performance-20261101/README.md) establishes
no observed frame-rate or timestamp-preservation failure requiring a replacement
PCM transport. Its GDP/TCP workload is not a substitute for authenticated GIO
production/lifecycle tests; [M5's transport audit](m5/TRANSPORT_DECISION.md) and
[task12 signed Android evidence](m5a/signed-native-integration-20261101/README.md)
provide those distinct checks.

48kHz, stereo, S16LE is192000 logical payload bytes/second. A10ms block is1920
bytes;20ms is3840; the60Hz test uses800 sample-frames/3200bytes per block.
Two boundaries carry384000 logical PCM bytes/second before protocol overhead.
Those numbers describe payload volume, NOT measured memory bandwidth or CPU cost.
For reference1080p60 raw NV12 is186624000bytes/s and BGRA497664000bytes/s; this
size difference alone does not prove that audio processing is inexpensive.

## Copy and resource accounting

- Producer PCM allocation/filling, GDP serialization and socket/GIO transport may
  allocate/copy. The production receiver's stack read → GstBuffer fill is a known
  user-space copy. GDP depayload, kernel transfers, conversion/resampling and device
  output can add work. No exact universal total copy count is advertised.
- The M5 hot reused-buffer microbenchmark measured the explicit3840-byte fill
  primitive at71.23ns on that earlier Linux setup. It excludes allocation,
  scheduling, GDP/kernel/device work and cache misses; do not extrapolate it to
  whole-session CPU or latency.
- [Current short resource observations](m5a/multihop-performance-20261101/resources/README.md)
  retain CPU/RSS and trace-flush costs. Windows total process CPU rose from0.891s
  video-only to5.391s A/V in the300-frame fixture. Startup/plugin loading and
  all verification work are included; this is not isolated memcpy cost. Android
  also showed higher total CPU. These costs remain visible, not assumed zero.
- Sustained A/V retained3720/3720frames and2976000 stereo PCM sample-frames per
  case. This meets the declared workload gate, not a general production load SLA.
  M6 must separate startup from steady-state CPU, profile actual GDP/GIO hot paths,
  repeat measurements and evaluate concurrency/soak before making broader claims.

## Negotiation and bounded behavior

1. Advertise audio as CPU PCM/serialized transport; video may independently use
   native shared memory. Video allocation identity does not cover audio buffers.
2. A strict requirement for zero-copy audio must reject as unsupported. Do not
   silently satisfy it with GDP/GIO, or downgrade a video strict requirement.
3. Continue preserving PTS, duration, segment mapping, session generation and
   relative A/V offset on one timeline. Socket arrival does not replace media PTS.
4. Bound audio queues in samples/bytes/time, independently of video image count.
   No mandatory pool-fill delay or fixed playback reservoir per transport hop.
   Backpressure/discontinuity/overrun must be observable; dropping PCM silently
   or rebasing just audio to conceal overload is unacceptable.
5. The final output schedules playback and exposes its actual device buffering.
   Preserved transport PTS and arrival skew are not physical lip-sync proof.
6. Existing signed lease/authentication and stream lifecycle remain unchanged.
   The test-only unauthenticated loopback helper is never a production API.

## Reopen shared-PCM transport only against evidence

Revisit if a declared concurrent-stream/latency/CPU budget fails on the production
path and profiling attributes a material fraction to avoidable PCM copies or
serialization. First compare bounded batching/allocation reuse/queue scheduling
without changing timing semantics. A proposed shared ring must define cross-UID
permissions, sample layout, rate/channel negotiation, generation/discontinuity,
producer/consumer ownership, bounded overrun/underrun, reconnect and crash safety.
It must demonstrate a matched measurable improvement while retaining negative
controls. Reuse suitable platform/GStreamer mechanisms before inventing a new
protocol. No such replacement is required or implemented by M5a.

Acceptance: decision and existing copy inventory published, payload arithmetic
checked, current performance/resource evidence linked, limitations and explicit
strict rejection recorded. This is a policy deliverable; it adds no runtime code.
