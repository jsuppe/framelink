# FrameLink + vkrender — executable roadmap checklist

Updated 2026-11-01. This is the checkable execution view of
[the milestone roadmap](REMAINING_ROADMAP.md); the detailed acceptance contracts
remain authoritative. Checked means implemented, reviewed and validated within
the stated scope—not universal hardware support or physical-output qualification.

Remaining execution order: **M5a → M6 → M7**, one task at a time. No concurrent
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

**State: approved, not started.** Deliverables: versioned allocation/usage
contract, native-memory GStreamer sink **and source**, platform adapters,
Windows decoder-to-independent-renderer example, and measured multi-hop evidence.

- [ ] **M5a.1 Usage/capability contract:** required vs preferred CPU read/write,
  GPU sampling/rendering and decoder output; version compatibility and rejection.
- [ ] **M5a.2 Platform adapters:** resource type, device/adapter identity,
  format/planes/stride/modifiers, import/export/forwarding and fence capabilities
  for supported Windows D3D, Linux DMA-BUF and Android AHardwareBuffer paths.
- [ ] **M5a.3 Pool allocation and ownership:** producer-owned vs consumer-provided
  allocations; preserve allocation identity and prevent premature reuse until
  every downstream CPU/GPU reader completes, including after crashes/reconnects.
- [ ] **M5a.4 Transport/presentation separation:** preserve shared A/V PTS and
  clock mappings; no pool-fill gate, independent per-track rebasing or fixed
  playback reservoir at each hop. Final output owns presentation scheduling.
- [ ] **M5a.5 Small pools and overload policy:** independently bound allocation
  capacity, in-flight work and queue age; test two/three-image paths where safe;
  make block/drop/reject policy and every loss visible.
- [ ] **M5a.6 Strict vs fallback:** explicit copy/conversion permission; strict
  zero-copy rejects incompatible paths. Define conflicting-consumer behavior.
- [ ] **M5a.7 Native-memory GStreamer sink and source:** remote requirements in
  caps/ALLOCATION/pools, safe renegotiation on format/device/generation changes,
  unchanged A/V session timing and legacy behavior.
- [ ] **M5a.8 Windows GPU-decoder example:** GPU GstBuffer → FrameLink sink →
  separate consumer renderer, with tested decoder/adapter/formats and explicit
  CPU-readable staging/readback alternative. Qualify NV12 conversion location.
- [ ] **M5a.9 Pass-through relay and transform output:** forwarding the same
  allocation across processes; distinguish required processing from boundary copies.
- [ ] **M5a.10 Instrumentation:** selected usage/memory/device, fallback reason,
  queue occupancy, waits, ownership and actual copies/readbacks/uploads.
- [ ] **M5a.11 Contract/negative tests:** hard/preferred requirements, malformed
  offers, old peers, conflicting consumers and strict-copy rejection.
- [ ] **M5a.12 Platform integration tests:** GPU-native and CPU-readable modes,
  incompatible devices/layouts, renegotiation, fences, stale generations and cleanup.
- [ ] **M5a.13 One-hop vs multi-hop qualification:** identical sources; account
  each identity, PTS and A/V offset; measure per-boundary residence/arrival jitter,
  payload copies, delayed readers/backpressure and safe reuse. Set numeric budgets
  before qualification; no claim of zero elapsed time or universal two-buffer FPS.
- [ ] **M5a.14 Audio copy-cost decision:** report existing GDP/GIO separately;
  evaluate shared PCM only against a demonstrated requirement. Video sharing
  does not establish audio zero-copy or eliminate final playback buffering.
- [ ] **M5a.15 Publish/review/merge:** supported-path benchmarks, capability
  matrix, integration guide and passing gates for the actual final revisions.

Acceptance: [shared-buffer boundary contract](M5A_PIPELINE_BOUNDARIES.md).
Strict pass-through must not silently copy pixels or add intentional per-hop
playback delay; unsupported hardware paths must reject clearly.

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
