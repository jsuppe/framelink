# M6 low-latency and native-memory resolution plan

Revision 1 — 2026-11-04. Owner: project executor, one task at a time.
Status: approved user requirements plus implementation proposals; no new runtime
qualification is claimed by this document. Applies to FrameLink and vkrender.

## Confirmed requirements and evidence boundaries

Jon confirmed on November 4 that the primary use case is zero-added-latency
process boundaries, not 150 ms buffering. Preserve original A/V timing across
hops and use native allocation sharing where compatible. Pool capacity is not a
playout delay. Mandatory ownership and readiness synchronization must never be
removed to obtain a lower latency number. Literal zero elapsed time is not a claim.

M1–M5b remain complete within their published scope. M6.1's existing matrix and
150 ms measurements remain historical evidence, not acceptance of the primary
low-latency mode. M6.2 is incomplete; a722b75 repairs receive queue handling but
post-fix full qualification is not established here. M6.8/M6.9 are open gaps.
Keep existing order M6.2–M6.9, then M7. M6.7 is automation infrastructure, not
permission to close M6 before M6.8 and M6.9 and their final integrated reruns.

## Decision register

For every proposal below record: accepted/rejected/superseded, decision owner,
date, rationale, public API/default impact, implementation revision and evidence.
Do not silently promote a recommendation to a user decision. Jon owns product
tradeoffs; executor owns routine implementation choices within accepted scope.

### D1 — Timing ownership and defaults

**Confirmed:** no intentional per-hop buffering in the primary mode.
**Proposed:** transport-only boundaries immediately expose ready buffers and
preserve the common media timeline; only the final playback stage schedules
presentation. Set the primary session/example profile to zero added transport
latency; retain explicitly selected buffered playback for compatibility.
Inventory all BaseSink sync flags, bin latency, app queues and renderer waits.
Do not set every final audio/video sink to sync=false: that destroys playback
synchronization. Do not independently rebase audio and video at each hop.

Alternative: retain 150 ms default and require opt-out. Not recommended because
it makes the primary use case opt-in and can conceal throughput failures.
Implementation decision (executor, November6): apply the confirmed zero-added
transport requirement to the public `framelinkavsrc` default (0ms). Retain the
existing explicit `latency-ms=150` setting for compatibility instead of adding a
redundant profile API. Migration notice and scheduled-playback regressions are
required; no claim of a silently compatible default change. Application-managed
clock setup and explicit final-playback samples remain unchanged.

### D2 — Queue capacity, overload and loss semantics

**Proposed:** default to the smallest validated ownership pool, test two buffers
first, and permit additional allocations only with a measured ownership reason.
No fill-before-delivery or queue-depth-derived delay. Keep pool size, unread
queue bound and scheduling latency independent. Publish actual effective values;
strict negotiation rejects unsupported depth rather than silently clamping it.

A finite-memory transport cannot guarantee zero waiting, zero drops and lossless
operation under an arbitrarily stalled consumer. Propose explicit policies:
- realtime/latest: bounded stale-video replacement, reported identities and age;
- lossless: bounded backpressure, with latency allowed to rise and reported;
- qualification/strict: fail on unexpected loss or unsupported allocation policy.
Never overwrite a GPU-owned frame. Audio must not silently use latest-frame-wins;
retain sample continuity or explicitly signal discontinuity/failure.
Decision needed: realtime overload behavior and API default. Retain current
fail-fast behavior until this choice is resolved; do not change it just to pass.

### D3 — Native producer integration and fallback

**Proposed:** bind M5a allocation/usage negotiation and completion fences to
coordinated sessions (M6.9), both individual elements and A/V bins. Support
upstream allocation into a negotiated pool where compatible, and external
allocation import where supported. Preserve identity, format/planes, stride,
modifier, adapter, generation and lifetime through every boundary.

Strict zero-copy rejects incompatibility before media. Optional copy fallback
must be explicitly selected and report reason, bytes and direction (CPU copy,
upload, readback, conversion). GPU-to-GPU copies are copies too. Generic decoder
compatibility is not established by a synthetic producer alone.
Decision needed: whether convenience callers default to strict or explicit
fallback profile. Recommendation: strict in the native profile; separately named
CPU/compatibility profile, never a hidden fallback.

### D4 — CPU versus GPU memory placement

**Proposed:** negotiate intended consumer access rather than assuming all native
memory is CPU-efficient. For CPU-heavy consumers evaluate cached shared system
memory versus explicit staging/readback; for render/decoder consumers preserve
native GPU allocations. Unified-memory platforms still need cache/fence checks.
Mixed CPU/GPU consumers must report the chosen compromise and actual transfer
costs. Do not promise simultaneous optimal placement or universal zero-copy.

Linux M6.8 compares cached memory, streaming access and staging on the same
workload. Android M6.5 isolates NV12 conversion, lock/cache and stride costs;
unsupported exact layouts must reject. Windows qualifies readback, adapter
compatibility and queue behavior rather than assuming Linux findings apply.
Selection depends on measured support; no vendor-specific solution is mandated.

### D5 — Qualification budgets

**Confirmed:** 1080p60 and 4K30 remain target workloads, not tiny-frame substitutes.
**Proposed gates:** zero configured transport hold; no unexplained nominal loss;
no hidden copies in strict native mode; bounded memory/leases; exact propagated
A/V timestamp offset. Test one and two boundaries with matched source work.

Before each campaign freeze numeric per-platform p50/p95/p99/max boundary-age,
CPU/GPU and overhead budgets. Proposed initial added-hop p95 target: <=2 ms;
this is a proposal, not a measured universal guarantee (older evidence includes
~2.1 ms). Report misses, do not tune the budget after observing a failing run.
Physical scanout/acoustic alignment are separate from API completion and PTS.

## Executable work breakdown

All unchecked tasks below are pending, not newly completed by prior large-queue
runs. Every validation records revision, host/driver, settings, exact command,
trace state, all failures, and raw evidence. Execute sequentially.

### M6.2 — Current task: low-latency sustained A/V

2a source inspection completed: [timing audit](TIMING_AUDIT.md). 2b partial: independent timing, bounded output, paired admission/residence probes,
all-frame timing and explicit preroll controls are validated in
[platform controls](zero-delay-platform-controls/README.md). Primary-profile
budget acceptance remains open; these measurements do not prove physical output.
- [x] 2a Audit timing/queue behavior in bins, elements, renderer and benchmark;
      publish where each wait/copy occurs and reconcile D1/D2 with current code.
- [ ] 2b Parameterize latency, pool and unread depth independently; add primary
      zero-added-delay and explicit buffered-control profiles; expose effective
      settings. Reopen M6.1 budget acceptance for the primary profile.
- [x] 2c Review a722b75 ownership, fence disposal, queue shrink, disconnect,
      monotonic release and actual depth; test unsupported-depth rejection.
- [ ] 2d Add deterministic fast-consumer, delayed-consumer and burst controls:
      distinguish producer gaps, overwritten pending frames, GPU-held slots,
      clock scheduling and upstream loss. Verify no wait-to-fill.
- [ ] 2e Run affected full suites on Linux/Windows/Android; then 30-minute
      supported 1080p60/4K30 cases with zero-added-delay/small pools. Preserve
      startup, steady-state and drain accounting. Record other host load.
- [ ] 2f Publish all pass/fail cells and queue-age distributions. Large-pool or
      150 ms control success cannot waive the low-latency gate.

Receive-depth gate evidence: [review and platform tests](receive-depth/README.md).

### M6.3 — Lifecycle endurance
- [ ] 3a At least 100 restart/reconnect cycles per target in the primary profile.
- [ ] 3b Include outstanding native leases, generation changes, EOS and pause;
      ensure cancellation is bounded and resources return to warm baseline.

### M6.4 — Fault and overload qualification
- [ ] 4a Resolve D2 before changing public overload defaults.
- [ ] 4b Inject consumer/GPU stalls, producer bursts, endpoint death, audio
      underruns and malformed/stale metadata. Validate each selected policy.
- [ ] 4c Verify bounded recovery, explicit loss/discontinuity, and no unsafe reuse.

### M6.5 — Performance and platform cost analysis
- [ ] 5a Measure per-stage copy/lock/wait/convert/present cost and resource growth.
- [ ] 5b Isolate Android NV12 CPU costs with format/layout and lock controls.
- [ ] 5c Separate Windows queue timing from producer and GPU scheduling defects.
- [ ] 5d Publish platform/format/resolution limits; no unsupported-cell omission.

### M6.6 — Observability without hidden overhead
- [ ] 6a Join independent source identities through each boundary, GPU completion
      and presentation submission; separately track PCM and A/V offset.
- [ ] 6b Compare trace off/on, quantify memory per recorder and loss detection;
      require bounded configurable recorder capacity, not huge implicit defaults.

### M6.7 — Repeatable automation (closure infrastructure)
- [ ] 7a Add deterministic queue/ownership/timing negatives to CI.
- [ ] 7b Publish reproducible real-device scripts and machine-readable gates.
- [ ] 7c Ensure final acceptance references the exact integrated revision after
      M6.8/M6.9; old benchmark passes are not final qualification.

### M6.8 — CPU-consumer memory path repair
- [ ] 8a Reproduce Linux slow/bimodal readback with allocation/cache diagnostics.
- [ ] 8b Compare cached shared-memory, streaming and explicit staging choices;
      implement negotiated CPU-efficient path without mislabeling copies.
- [ ] 8c Validate pixels, planes, visibility, lifetime and A/V timing; measure
      target-rate CPU consumption on all three platforms where supported.

### M6.9 — Zero-copy native producer in coordinated sessions
- [ ] 9a Connect native negotiation to session coordinator, sink and A/V bin.
- [ ] 9b Integrate upstream allocation/import and readiness/completion fences for
      DMA-BUF, D3D11 and supported AHardwareBuffer layouts.
- [ ] 9c Reject incompatible adapter/layout/usage and test explicit fallback.
- [ ] 9d Qualify real compatible decoder/renderer producers, native consumer,
      and two-hop relay with PCM; prove allocation identity and zero payload
      copies, not just absence of CPU mapping.
- [ ] 9e Repeat affected endurance/performance/trace gates on the final integrated
      revision, review all unresolved decisions and verify CI/merge before M6 closes.

### M7 — Release and developer handoff
- [ ] 7.1 Freeze API/ABI, decision records, capability matrix and timing migration.
- [ ] 7.2 Ship zero-added-delay Windows/Linux/Android examples, native decoder
      integration recipes and an explicitly buffered playback example.
- [ ] 7.3 Validate clean installation and observable effective settings/copy paths.
- [ ] 7.4 Review exact-head tests, merge/tag/artifacts and known limits; retain
      physical-output exclusions unless independently measured.

## Immediate next action and coordination

November6 20:30 EST: parent executor owns M6.2 sequentially. All four Windows CPU 1800s cases have complete counts and zero diagnostic timing misses, but this is not milestone completion. Pre-repair Windows BGRA native, NV12 native and BGRA CPU exceeded the existing64KB/min memory-growth gate; repaired NV12/BGRA native and BGRA CPU now pass; remaining CPU-rate/format regression at the new cache revision is in progress; see [resource reconciliation](zero-delay-platform-controls/RESOURCE_GATES.md). Original Windows159ms native stall remains unproven as repaired. Linux114/114, Windows112/112 and Android72/72 full-suite evidence exists at the recorded platform revisions; final integrated qualification remains required.

Source-only Windows NV124K30 baseline completed1800s at7.36KB/min privatecommit, below64KB/min; no transport claim. Local-consumer instrumentation control e109432 passed5s exact-count/negative/smoke checks on Linux and Windows. Its1800s baseline passed counts/timing with7.34KB/min growth. Native-wrapper A/B attributed avoidable growth to per-frame temporary layout probing; e19e335 caches measured size/pitch per slot and generation without retaining leases or changing fences. Windows affected5/5 plus resize/policy2/2 and Linux smoke passed. Full rebuild stopped before CTest on a test-only G_CALLBACK comma error; corrected89543da and native-source/resize plus copy-negative2/2 passed. Full Windows rebuild/CTest retry passed112/112 (556.13s, exact revision recorded in windows-native-layout/full-retry-status.json). Native NV124K30 layout-cache1800s passed54000video/86400000PCM, zero34ms misses, AVmapping0ns; privatecommitconsumer6.96/producer45.72KBmin both pass64KB/min. Native NV12 resource repair validated; BGRA native5s preflight passed150video/240000PCM. Native BGRA4K30 layout-cache1800s also passed54000video/86400000PCM, zero34ms misses, AVmapping0ns; consumer6.93/producer6.52KBmin pass64KB/min. Both native4K resource repairs are validated. BGRA4K30 local-consumer1800s completed54000video/86400000PCM, zero misses and7.31KB/min growth, matching NV12local7.34; it does not reproduce BGRA CPU98.11KB/min. CPU source allocates a fresh33MB GstBuffer each frame while local test sources reuse buffers. Allocation probe passed ownership negatives and found40.58million pagefaults/5000frames versus0 with two-buffer reuse; allocation/write18.89s versus6.14s. Neither arm reproduced retained growth (0–4KB), so no leak attribution. Implemented Windows-only two-buffer nonblocking CPU cache with allocation fallback when both are held, invalidation on resize/stop/finalize; downstream ownership and native path unchanged. Windows affected6/6 passed, including four retained CPU samples beyond cache capacity and across stop/start. Added explicit CPU grow/shrink generation test that retains old samples through resize and source destruction, checking exact memory extent, PTS and per-plane pixels. CPU grow/shrink/teardown and native copy-negative2/2 passed, plus Linux rebuild/modified roundtrips2/2. Reviewed production/test change committed29bbddb. Full Windows rebuild/CTest passed112/112 (554.76s). BGRA CPU4K30 short preflight passed150video/240000PCM, zero misses. BGRA CPU4K30 on29bbddb passed1800s:54000video/86400000PCM, zero34ms misses, AVmapping0ns, privatecommitconsumer7.14/producer7.18KBmin (both under64). Thus all three originally failing resource cells have repaired-revision passes, without claiming a precise retained-growth leak cause. Current cache CPU sustained matrix3/4: NV12CPU1080p60 passed108000video/86400000PCM, zero17ms misses, AVmapping0ns; consumer3.25/producer7.34KBmin. BGRA CPU1080p60 also passed108000video/86400000PCM, zero17ms misses, AVmapping0ns; consumer7.18/producer7.22KBmin. Sole active executor is final CPU cell NV124K30 at29bbddb (150frame preflight passed), then deterministic M6.2 controls. Original159ms stall is not claimed repaired; BGRA CPU resource acceptance passes at29bbddb but the exact historical growth cause is not proven. Keep original failures and no physical-output/zero-copy claim. M6.3–M6.9 remain open in order. Notify Jon only at full M6 closure and hold before M7 for maintenance; periodic status automation remains cancelled.
