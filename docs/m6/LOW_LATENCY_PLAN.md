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

### M6.2 — Acceptance suspended; evidence published

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
- [x] 2d Add deterministic fast-consumer, delayed-consumer and burst controls:
      distinguish producer gaps, overwritten pending frames, GPU-held slots,
      clock scheduling and upstream loss. Verify no wait-to-fill.
- [ ] 2e Run affected full suites on Linux/Windows/Android; then 30-minute
      supported 1080p60/4K30 cases with zero-added-delay/small pools. Preserve
      startup, steady-state and drain accounting. Record other host load.
- [x] 2f Publish all pass/fail cells and queue-age distributions. Large-pool or
      150 ms control success cannot waive the low-latency gate.

Deterministic2d evidence: [queue/clock controls](queue-controls/README.md), commit0ff18eb; Linux/Windows/Android passed. Held-reader controls emulate ownership delay, not physical GPU latency.

Receive-depth gate evidence: [review and platform tests](receive-depth/README.md).

### M6.3 — Current execution: lifecycle endurance
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

November7 01:58 EST: [primary results](PRIMARY_RESULTS.md) publish all14 supported1800s cells, queue distributions, original failures and exclusions. All selected current captures pass independent counts, frozen arrival diagnostics and64KB/min resources. Linux114/114, Windows112/112 plus finalqueuecontrol, Android72/72 and both Android ABI builds pass.2a/2c/2d/2f complete;2b/2e acceptance remains open.

**Suspended M6.2 acceptance:** original nativeWindows159ms event has no proven causal repair (owner Evelyn; resume with discriminating timing evidence, not endless clean repeats). Primary-profile latency/CPU/GPU budget definition is not approved; whole-path17/34ms diagnostics cannot stand in for isolated added-hop overhead. Owner Evelyn prepares measurement/decision evidence; Jon owns product tradeoff acceptance. No threshold is relaxed or chosen after a failure. This is an explicit dependency, not a completed task or milestone.

Per single-task blocked-work rule, advance one ready task inside the same milestone: M6.3a lifecycle endurance, 1080p60 native, pool2/unread1/output1/latency0. Existing benchmark supports long-lived producer/consumer and fresh peer per cycle. Preflight both directions, then100cycles per direction/platform with independent300video/240000PCM expected each5s cycle; retain every per-cycle result. Existing warm-after5 resource limits16KB/cycle,0.1handles/cycle,0.02threads/cycle remain unchanged. M6.3b outstanding leases/generation/EOS/pause needs separate coverage; restart completion alone cannot close it. Harness5712397 adds independent PCM counts, effective profile fields, retained cycle evidence; production unchanged. Linux two-cycle preflights in both directions passed. Linux100/100 cycles in both directions passed: independent300video/240000PCM percycle, memory2.17/0.81KB percycle, flat handles/threads. Evidence lifecycle/linux. Windows pilots passed;100x5s producer-restart delivered100/100 cycles but FAILED privatecommit21.10KB/cycle against16KB/cycle. Handles/threads declined; warm endpoints38540→37372KB also declined, but intermediate fluctuation produced a failing fit. Failure retained in lifecycle/windows-failure; no threshold adjustment. Consumer-restart100 subsequently completed all independent counts but FAILED privatecommit84.09KB/cycle; handles/threads declined (lifecycle/windows-consumer-failure). Local-consumer baseline d514fc7 passed100/100 exact-count cycles and privatecommit-1.994KB/cycle, non-growing handles/threads; it does not reproduce transport-native growth. RawD3D11 lifecycle reproduced31.225KB/cycle without FrameLink/GStreamerwrapping; cachedwrapped arm28.210KB/cycle. Both100cycles, finalCOMRelease0 eachcycle. This localizes a reproducible device/driver lifecycle contribution, not every byte of the original failure. Exclusive-device reuse probe passed100cycles at-0.384KB/cycle, final actualRelease0, supporting device churn as the reproducible contribution. No production global cache/sharing introduced. DXGI Trim teardown control completed100cycles at35.761KB/cycle, finalRelease0 eachcycle: negative result, archived lifecycle/d3d-trim-probe. No production Trim change. Original16KB/cycle gate remains failing. Both Windows directions remain unqualified. Ownership review rejects a process-global shared device (fence ordering/inter-channel isolation), a COM-refcount-based idle cache (external references/races), and implicit GPU destruction at DLL unload. A possible explicit caller-owned device lifetime needs adapter/removal validation, per-channel GPU ordering, outstanding-lease retention, and final release before unload; this is not implemented or claimed as default-path repair. Android producer-restart100cycle run rejected99/100: persistentconsumer exactcounts100/100 andstable0.36KB/cycle, butfreshproducer8 SIGABRT/std::bad_alloc. Symbolized vector<GstMessage*> append in avsink handleMessage identifies unsynchronizedaudio/video EOS collection anddrainer access. Originalfailure archived lifecycle/android-eos-failure. Current fix serializes collection/single-drainer publication/extraction with sessionmutex, bounds EOS tooneperchild, forwardsoutside lock. FocusedLinux12/12 forced-concurrentEOS pairs passed exactcounts/drain. Linuxfullproducer-bin andAndroid12focusedEOSpairs/fullproducer-bin passed, sourcehashes/logs archived lifecycle/eos-fix. Windows12focusedEOSpairs andfullproducer-bin also passed; allthreeplatforms affectedvalidation archived lifecycle/eos-fix. Fix ready for committedAndroid100cycle rerunbothdirections withunique eos-fixed labels andunchanged gates. Windowsresourcefailures remainopen, no devicecachechange oracceptancewaiver.

M6.2 stays unaccepted, M6.3 is active sequentially, M6.4–9 remain pending. No automated status reports. Notify Jon only at fullM6closure and hold beforeM7 for maintenance.
