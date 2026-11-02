# FrameLink + vkrender — executable roadmap checklist

Updated 2026-11-01. This is the checkable execution view of
[the milestone roadmap](REMAINING_ROADMAP.md); the detailed acceptance contracts
remain authoritative. Checked means implemented, reviewed and validated within
the stated scope—not universal hardware support or physical-output qualification.

Remaining execution order: **M5b → M6 → M7**, one task at a time. No concurrent
implementation/review/validation streams. Finish the current milestone before
starting the next. Hourly reporting is not an implementation executor.

## Completed milestones

- [x] **M1: Audio foundation.** PCM transport/timestamps, bounded fixtures and
  regression gates; FrameLink PR #2, `c67cde8`.
- [x] **M2: A/V session contract.** Versioned session/track negotiation, common
  timeline, endpoint/security and incompatibility contracts; PR #3, `1b02523`.
- [x] **M3: Independent producer and consumer apps.** Startup, verification,
  playback options and restart/failure matrix; PR #4, `148cc05`.
- [x] **M4: Synchronized playback and renderer integration.** Shared timeline,
  bounded scheduling, recovery/selection/mute and diagnostics; FrameLink PR #5
  `ed81cbb`, renderer PR #12 `4c2ff15`. Physical speaker/display synchronization
  and seamless independent-source drift correction are not implied.

## M5 — Platform deployment and transport qualification

**State: COMPLETE, within the documented capability scope.** FrameLink PR #6
merged as `ab7176e`; renderer PR #13 merged as `9f74b9d` on 2026-11-01 after all
final checks passed. No outstanding M5 implementation or merge gate; no input
needed from Jon. Explicit unsupported/unqualified paths remain listed below.

- [x] **M5.1 Desktop external pipeline handoff:** separate decoded producer and
  consumer, video/PCM/PTS checks and explicit failure/deployment checks.
- [x] **M5.2 Windows performance repair and qualification:** preparation/GO,
  GPU ownership/presentation semaphore, power/startup and finite-session handling.
  Traced BGRA/NV12 1080p60 cases each account for 4,200/4,200 identities over 70s;
  steady API-accepted presentation 59.993 fps. Untraced cases are aggregate proof.
- [x] **M5.3 Android authenticated broker and adoption:** signed leases,
  connected-FD ownership and GStreamer/SessionProvider binding.
- [x] **M5.4 Android separate-UID A/V lifecycle:** signed producer/consumer,
  BGRA/NV12 pixel/PTS and PCM verification, death/restart, focus/reopen and rejection.
- [x] **M5.5 Android real-audio defect:** reproduce and guard OpenSLES negative
  sample spans; retain ordinary small-drift resampling. Device matrix 20/20;
  BGRA 300/300 and NV12 450/450 with complete PCM; saved 60/60 sink-write audit.
- [x] **M5.6 Final integration evidence and capability decisions:** Linux 61/61;
  Windows 58/59 initial run plus corrected harness rerun, all 59 unique cases
  covered; external negatives 4/4 per desktop OS; dual-ABI Android builds.
  Retain GDP/GIO PCM transport and document its copies and deployment limits.
- [x] **M5.7 FrameLink final review/CI/merge:** final Linux/Windows CI passed
  after deterministic audio-fixture repair; PR #6 merged.
- [x] **M5.8 Renderer final review/CI/merge and milestone closure:** matching
  FrameLink pin and portable clock/scheduler/pacer/NV12 tests. Windows runtime-path
  CI gap repaired in `ecda871`, portable checks 13/13 on Caspar and Linux. Final
  [hosted run41309180883](https://github.com/jsuppe/vkrender/actions/runs/41309180883)
  passed Windows/Linux/Android/sender; PR #13 merge `9f74b9d` verified. Ledgers and
  roadmap reconciled. Canceled earlier run remains preserved, not labeled passed.

Deliverables/evidence: [capability matrix](m5/CAPABILITY_MATRIX.md),
[final integration results](m5/final-integration-20261101/README.md),
[FrameLink PR #6](https://github.com/jsuppe/framelink/pull/6),
[renderer PR #13](https://github.com/jsuppe/vkrender/pull/13).

## M5a — Native-memory negotiation and zero-copy pipeline boundaries

**State: COMPLETE within documented paths; PR #7 merged as `3e1d51f`.** Deliverables: versioned allocation/usage
contract, native-memory GStreamer sink **and source**, platform adapters,
Windows decoder-to-independent-renderer example, and measured multi-hop evidence.

- [x] **M5a.1 Usage/capability contract:** required vs preferred CPU read/write,
  GPU sampling/rendering and decoder output; version compatibility and rejection.
  [Contract](MEMORY_CONTRACT.md) and [validation](m5a/contract-20261101/README.md):
  Linux/Windows/Android device4/4 each, dual-ABI builds and sanitizers. Local
  policy helpers only; runtime extension exchange/adapter qualification remains.
- [x] **M5a.2 Platform adapters:** resource type, device/adapter identity,
  format/planes/stride/modifiers, import/export/forwarding and fence capabilities
  for supported Windows D3D, Linux DMA-BUF and Android AHardwareBuffer paths.
  [Actual-resource checks](m5a/adapters-20261101/README.md): Linux/Windows/Android
  6/6 each. Unknown native layout/device facts remain explicit; external-pool
  adoption/relay are not advertised until implemented. Driver probes remain gated.
- [x] **M5a.3 Pool allocation and ownership:** producer-owned vs consumer-provided
  allocations; preserve allocation identity and prevent premature reuse until
  every downstream CPU/GPU reader completes, including after crashes/reconnects.
  Subtask3a reservation safety locally validated: [evidence](m5a/ownership-20261101/README.md).
  Local external-pool adoption validated: [API and limits](EXTERNAL_POOLS.md).
  [Bounded reader leases](READ_LEASES.md), native completion handoff, Windows
  transactional import cleanup and separate-process RGBA8 GPU reads now pass.
  [GPU/lifecycle evidence](m5a/read-lease-gpu-20261101/README.md): normal and abrupt
  producer-death cases 2/2 on Linux/Windows/Android; delayed-reader/reconnect
  controls pass. External decoder negotiation, distributed relay and broader
  format/device qualification remain tasks7–13, not implied by this core API gate.
- [x] **M5a.4 Transport/presentation separation:** preserve shared A/V PTS and
  clock mappings; no pool-fill gate, independent per-track rebasing or fixed
  playback reservoir at each hop. Final output owns presentation scheduling.
  [Timing contract](TRANSPORT_TIMING.md): two-hop future-PTS/offset tests and
  scheduling negative3/3 on Linux/Windows/Android; fixed-rate scope, no throughput
  or arbitrary-duration claim. Existing defaults retained.
- [x] **M5a.5 Small pools and overload policy:** independently bound allocation
  capacity, in-flight work and queue age; test two/three-image paths where safe;
  make block/drop/reject policy and every loss visible.
  [Pool policy](POOL_POLICY.md): two/three-image saturation, bounded wait/drop/error,
  delayed readers, opt-in receiver age and counters. Linux7/7, Windows7/7 and
  Android policy suite/dualABI pass; no end-to-end latency or full-rate claim.
- [x] **M5a.6 Strict vs fallback:** explicit copy/conversion permission; strict
  zero-copy rejects incompatible declared paths. Conflicting consumers cannot
  override each other. [Boundary policy](m5a/boundary-policy-20261101/README.md):
  Linux/Windows/Android7/7 each, dualABI builds. Runtime enforcement is task7.
- [x] **M5a.7 Native-memory GStreamer sink and source — locally validated, not merged:**
  explicit Linux DMA-BUF, Windows D3D11 and Android RGB AHB source/sink adapters;
  bounded ALLOCATION pools with writer/reader completion contracts; authenticated
  SessionProvider policies and actual platform binding adapters. Remote requirements
  are applied to ALLOCATION; properties and pool generation are frozen. Changed pools
  reject before startup/delivery/submission and require a fresh session, while legacy
  resize remains unchanged. Variable PTS/DTS/duration/discontinuity/generation and
  legacy behavior are tested. See [final policy evidence](m5a/gst-authenticated-allocation-20261101/README.md)
  and [native adapter contracts](GST_NATIVE_MEMORY.md).
  This qualifies explicit adapters, not arbitrary stock decoders/readers. Windows
  decoder, signed full native Android pipeline, multi-hop and throughput remain
  downstream qualifications; no physical-output claim.
- [x] **M5a.8 Windows GPU-decoder example — locally validated, not merged:**
  RTX5090/GStreamer1.28 H.264 NV12 → GPU BGRA conversion into FrameLink-owned
  storage → native sink → separate swapchain consumer. Explicit CPU-readable
  download/CPU-convert/upload alternative also passed60/60 frames, GPU completions,
  accepted presents, color-bar checks and duration checks with pool2 and zero gaps.
  [Runner/guide](../samples/D3D_DECODER.md), [evidence](m5a/windows-decoder-20261101/README.md).
  Stock decoder arrays are non-shareable (NT export0/30): raw NV12 pass-through
  remains unsupported on this decoder. Color conversion is explicit, not claimed
  as raw decoder zero-copy. No sustained-rate or physical-scanout claim.
- [x] **M5a.9 Pass-through relay and transform output — locally validated, not merged:**
  bounded read-only relay retains upstream ownership until downstream completion;
  native identity matching, reversed slots, exact timing, wrong-identity rejection,
  slow-reader and disconnect pinning pass on Linux/Windows/Android. Linux three-
  process NV12 test16/16 passed; Windows/Android fixtures are local ownership tests.
  [Guide](../samples/NATIVE_RELAY.md), [evidence/remaining qualification](m5a/native-relay-20261101/README.md).
  GPU transformed output is task8's explicit color conversion. GPU relay performance,
  Windows/Android three-process and production broker/recovery qualification remain
  M5a.12–13 gates; no automatic-safe-recovery claim on peer death.
- [x] **M5a.10 Instrumentation — locally validated, not merged:** typed opt-in
  policy/resource/device decisions, observed logical copies/uploads/readbacks and
  retained-reader occupancy alongside existing waits/identity events. Linux11/11,
  Windows8/8, Android8/8 and dual-ABI builds; native/legacy accounting positive and
  missing/lost/empty negative controls. [Contract](MEMORY_TRACING.md),
  [evidence](m5a/memory-instrumentation-20261101/README.md).
  Enabled overhead and driver-internal work are not hidden; throughput stays task13.
- [x] **M5a.11 Contract/negative tests — locally validated, not merged:**
  hard/preferred requirements, malformed offers, asymmetric extension/old-peer
  rejection, conflicting consumers, strict-copy enforcement and frozen generations.
  New15-case hostile-wire fixture includes proof reflection and forged approval;
  Linux7/7, Windows6/6, Android6/6 and dualABI. [Evidence](m5a/contract-negatives-20261101/README.md).
- [x] **M5a.12 Platform integration tests (qualified paths):** GPU-native and CPU-readable modes,
  incompatible devices/layouts, renegotiation, fences, stale generations and cleanup.
  Three-process GPU relay16/16 each Windows/Linux/Android; signed native Android
  A/V18/18 lifecycle/security cases, exact RGBA/PTS/PCM and GPU-completion retention.
  [Device evidence](m5a/signed-native-integration-20261101/README.md); unsupported
  decoder/layout and physical-output limits retained. Performance remains task13.
- [x] **M5a.13 One-hop vs multi-hop qualification (qualified paths):** identical sources; account
  each identity, PTS and A/V offset; measure per-boundary residence/arrival jitter,
  payload copies, delayed readers/backpressure and safe reuse. Set numeric budgets
  before qualification; no claim of zero elapsed time or universal two-buffer FPS.
  [Results and exclusions](m5a/multihop-performance-20261101/README.md): supported
  nominal cases3720/3720 at~60fps, complete simultaneous PCM, exact media offset,
  zero accepted trace loss, explicit faults/copies/resource costs. Cold startup,
  unmeasured formats and physical output remain unqualified.
- [x] **M5a.14 Audio copy-cost decision:** report existing GDP/GIO separately;
  evaluate shared PCM only against a demonstrated requirement. Video sharing
  does not establish audio zero-copy or eliminate final playback buffering.
  [Policy and measured-cost limits](AUDIO_MEMORY_POLICY.md): retain GDP/GIO,
  advertise CPU/serialized PCM separately, reject strict zero-copy audio, and
  require production profiling/budget evidence before a shared-PCM redesign.
- [x] **M5a.15 Publish/review/merge:** supported-path benchmarks, capability
  matrix, integration guide and passing gates for the actual final revisions.
  Final source `e38de30`: Linux97/97, Windows95/95, hosted Linux/Windows CI passed;
  latest signed Android6/6. [Final evidence](m5a/final-integration-20261101/README.md).

Acceptance: [shared-buffer boundary contract](M5A_PIPELINE_BOUNDARIES.md).
Strict pass-through must not silently copy pixels or add intentional per-hop
playback delay; unsupported hardware paths must reject clearly.

## M5b — GStreamer audio elements and coordinated A/V sessions

**State: in progress; task1 contract specified, task2 next.** Added at Jon's request 2026-11-01. Execute
only after M5a closes, before M6 qualifies the complete public integration path.
Video elements remain video-only; separate audio elements share one session.

Deliverables: `framelinkaudiosink`, `framelinkaudiosrc`, reusable session
coordination, integration examples, timing/security/lifecycle tests and platform
qualification. Initially wrap the existing PCM/GDP/GIO transport; do not introduce
shared-memory audio or advertise A/V zero-copy. Producer and consumer A/V
convenience bins are required deliverables (approved 2026-11-01), implemented
after the individual elements and session/timing behavior are validated. They
expose separate audio/video pads, not a new multiplexed format; individual
elements remain independently available.

- [x] **M5b.1 Public element/session contract:** define pad caps, supported PCM
  formats/rates/layouts, properties, session/track identifiers, discovery/auth,
  standalone audio mode and opt-in coordinated A/V mode. Unsupported caps fail
  clearly; retain existing video-only application behavior.
  [Public contract](GSTREAMER_AV_CONTRACT.md): explicit v2 optional-track semantics,
  typed shared context, bounded lifecycle and separate-pad bin names. Design only;
  factories/runtime qualification remain subsequent tasks.
- [ ] **M5b.2 Reusable session coordinator:** share authenticated negotiation,
  track readiness, common epoch/clock mapping and config generation across audio
  and video elements. Define GstContext/API propagation and bounded missing-peer
  behavior without requiring sample-application internals.
- [ ] **M5b.3 Audio sink:** wrap existing bounded PCM transport, negotiate caps,
  carry timestamps/durations/discontinuities, handle flow errors and backpressure;
  report explicit serialization/copy costs rather than imply shared PCM.
- [ ] **M5b.4 Audio source:** receive PCM with exact sample order and timing,
  reconstruct buffers/caps, expose appropriate live/latency behavior and integrate
  with standard playback sinks using the shared pipeline clock.
- [ ] **M5b.5 Common A/V timing:** map TIME segments into one common timeline,
  preserve intentional offsets, handle missing/invalid PTS, GAP, DISCONT, EOS,
  flush and new segment epochs. Never independently zero audio and video origins;
  define supported seek/rate-change behavior and reject unsupported modes.
- [ ] **M5b.6 Lifecycle/security:** both startup orders, coordinated pause/resume,
  track loss, cancellation, peer death/reconnect, stale generations, negotiation
  rejection, bounded cleanup and platform endpoint permissions/authentication.
- [ ] **M5b.7 Integration examples:** audio-only and two-process A/V GStreamer
  examples, with Windows integration first, then Linux/Android adapters. Show
  independent video rendering plus standard audio output, shared session setup
  and CPU/native-video compatibility without hand-wired GDP internals.
- [ ] **M5b.8 Automated validation:** GstTestClock/segment tests, exact PCM and
  producer-to-consumer A/V offset checks, skew/jitter accounting, negative controls,
  restart/failure scenarios and regression tests for existing video-only elements.
- [ ] **M5b.9 Producer A/V bin:** compose the validated video/audio sinks with
  the shared session coordinator; expose separate audio/video sink ghost pads and
  one documented session configuration. Propagate caps, segments, EOS, flush,
  state changes and errors without adding independent timestamp rebasing or a
  hidden per-track latency reservoir. Final public element name is set in M5b.1.
- [x] **M5b.10 Consumer A/V bin:** compose the validated video/audio sources;
  expose separate audio/video source ghost pads for independent downstream
  rendering/playback. Coordinate readiness, session clocks and reconnect; do not
  hardwire a display or audio device. Preserve native video memory negotiation.
- [x] **M5b.11 Bin parity and examples:** demonstrate standalone-element and
  bin-based two-process pipelines. Test equivalent PCM/video timing and negotiated
  memory paths, missing-track policy, startup/pause/flush/EOS, peer death/reconnect,
  error propagation, queue bounds and cleanup. Document properties/pad names and
  supported audio-only/video-only behavior without silently waiting forever.
- [x] **M5b.12 Platform qualification:** Windows, Linux and Android builds/runtime
  on supported targets; verify native video plus audio, audio-only, latency
  queries, queue bounds, resource cleanup and tracing/copy accounting for both
individual elements and convenience bins. Separate
  software timing evidence from physical speaker/display alignment.
- [x] **M5b.13 Publish/review/merge:** element reference, copy/timing contract,
  developer migration guide, examples, final-revision CI and reviewed evidence.
  Feed these elements into M6's sustained/performance qualification matrix.

Acceptance: a developer can split an A/V GStreamer pipeline across processes
using either the individual public video/audio elements with shared-session
coordination or the producer/consumer convenience bins;
original relative A/V timing and PCM content survive the transport, recovery is
bounded, and audio copies/unsupported paths are explicit. Physical lip-sync and
new shared-PCM transport are not silently included.

## M6 — Reliability, performance and observability

**State: not started as a milestone.** Earlier targeted stress/trace tests are
baseline evidence, not completion of these release-scale tasks.

- [ ] **M6.1 Define qualification matrix and numeric budgets:** supported
  hardware/workload, FPS, latency/jitter, A/V offset, resources and tracing overhead.
- [ ] **M6.2 Sustained A/V:** 30-minute soaks on each supported target.
- [ ] **M6.3 Lifecycle endurance:** at least 100 restart/reconnect cycles per target.
- [ ] **M6.4 Fault qualification:** slow readers, underruns/drops, endpoint death,
  generation changes and cleanup; fix failures and rerun affected cases.
- [ ] **M6.5 Performance/resource evidence:** distributions, queue depth,
  CPU/GPU/memory/handles, clock behavior and bounded growth.
- [ ] **M6.6 Tracing qualification:** disabled vs enabled cost, record-loss
  detection and reproducible frame/audio accounting.
- [ ] **M6.7 Automate and close:** deterministic CI faults, reproducible benchmark
  commands and reviewed acceptance report with remaining unsupported modes.

## M7 — Release and developer handoff

**State: not started.** Deliverable: reproducible versioned release, not merely
source branches or a demo that depends on the developer's machine.

- [ ] **M7.1 API/version review:** compatibility and migration notes, final
  requirements-to-tests matrix and explicit release capability scope.
- [ ] **M7.2 Integration package:** producer/consumer and Windows GStreamer
  examples, invocation guides, deployment instructions and diagnostic troubleshooting.
- [ ] **M7.3 Clean-install validation:** dependencies, platform artifacts and
  runnable A/V workflow on clean supported machines.
- [ ] **M7.4 Final release gate:** CI on final revisions, review/merge, tag,
  artifacts, evidence and known-limit publication.

## Explicit limits — not silently checked off

- Current ordinary GStreamer video boundaries copy; native GPU sink/source and
  general external-allocation forwarding belong to M5a.
- Android production evidence is the documented CPU/Surface path at 160×90/30fps.
  It is not Android GPU-native GStreamer/renderer or Pi/Cuttlefish runtime parity.
- Android coordinated audio-master remains a diagnostic with a known stall;
  ProducerMapped is the qualified default. Do not advertise the diagnostic as
  supported unless separately repaired and qualified.
- API-accepted presentation is not physical scanout. Acoustic/physical lip sync,
  cold physical display wake, actual route unplug and seamless suspend remain
  unqualified/unsupported as listed in the capability matrix. The release gate
  must retain those limits or obtain new evidence before enabling those claims.
