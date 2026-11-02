# M5a.13 — qualified native-boundary performance result

Locally accepted, pending milestone final review/CI/merge. Predeclared numerical
budgets are in [PLAN](PLAN.md); measurements do not change default pool sizes or
production scheduling. All cases use two original images, no per-frame control
handshake, independently paced source, and explicit startup/warmup/drain.

## Accepted gates

- Matched one/two-hop, trace off/on,3720 total/3600 measured frames:
  Linux RGBA CPU-readable and GPU sampler; Android warmed RGBA GPU sampler;
  Windows BGRA/NV12 GPU-copy consumer. Every nominal case delivers3720/3720,
  approximately60freshfps, exceeding59.4fps budget. No unexplained loss/repeat.
- Native relay imports/forwards the same allocations; no relay payload mapping.
  GPU read completion gates original reuse. Trace join counts3720/7440 per case,
  GPU completion3720; zero loss in accepted captures. Typed identity, metadata,
  lease/copy controls are complemented by task9/12 allocation/lifetime tests.
- Added-hop p95 source-marker-to-arrival approximately1.6–2.1ms, below16.667ms.
  This is a difference of workload distributions, not the latency of every frame.
  Trace-on/off nominal FPS differences below2%, p95 differences below16.667ms.
- Sustained simultaneous A/V one/two-hop runs on all three platforms: complete
  PCM and all3720frames, unchanged37ms producer A/V media offset. Raw arrival
  and differential transport skew are reported separately, not called lip-sync
  playback proof. Exact sample counts, durations and segment running time checked.
-100ms delayed-reader controls retain GPU ownership. Linux/Android300/300 with
  backpressure. Windows300 produced/298 received;182/186 pending replacements
  explicitly joined to overwrite events and recovery reaches final identity.
  Fault losses do not relax nominal zero-loss acceptance.
- CPU/RSS and explicit control-thread trace-flush observations published separately;
  all nine short resource cases retain complete media. Analyzer negative tests8/8.

## Evidence

[Linux CPU](linux-cpu/), [Linux GPU](linux-gpu/), [Android GPU](android-gpu/),
[Windows native video](windows/), [Linux/Android A/V](av-linux-android/),
[Windows A/V](av-windows/), [resource/flush observations](resources/).
Each retains exact captures, analysis and build identifiers. Source generation
uses CPU writes/upload. Linux/Android consumer verification reads36 private bytes
per frame; Windows GPU copy/readback verifies a full private image. Those costs
are explicit, not silent FrameLink relay copies or an end-to-end zero-copy claim.

## Limits and retained failed controls

- Android cold first-use shader setup replaced a pending frame. Warmed qualification
  is accepted; cold first-render lossless delivery remains unqualified.
- Android initial A/V two-hop recorder overflow failed trace acceptance; larger
  bounded recorder repeat passed. The failed capture is retained.
- Default mailbox replacement under an arbitrarily stalled consumer is intentional
  overload policy, not guaranteed lossless delivery. Two buffers are proven for
  these measured workloads, not every decoder/effect/device.
- Linux/Android sustained GPU evidence is RGBA; Windows BGRA/NV12. This does not
  qualify Linux NV12 GPU sampling/throughput, opaque Android YUV-as-NV12 or other
  formats/devices. Existing strict capability rejection still applies. Decoder
  arrays and conversion restrictions remain those documented in task8.
- The paced A/V helper is same-host/same-UID test-only GDP/TCP PCM. Signed Android
  production lifecycle qualification is the separate task12 evidence; there is
  no claim of sustained cross-UID/decoder/renderer presentation from this fixture.
- Process CPU includes startup, RSS is not unique shared/GPU memory, and explicit
  flush excludes later automatic exit flush. M6 owns repeated steady-state cost
  attribution, longer soaks and expanded production integration qualification.
- Physical scanout, acoustic synchronization, suspend/cold-display wake, driver
  internal copies and arbitrary independent-clock correction are not established.

Task13 is locally accepted for these explicitly measured paths. It does not close
M5a: audio copy-cost decision (14), final integrated review/CI/docs/merge (15) remain.
