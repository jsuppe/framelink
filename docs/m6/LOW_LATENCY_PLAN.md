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
- [x] 5a Measure software call-boundary copy/lock/wait/convert/present costs and resource growth. Evidence/scope: cost/STAGE_COSTS.md. GPU execution, physical scanout and primary-renderer qualification are not inferred; remaining gates stay open.
- [x] 5b Isolate Android NV12 CPU costs with format/layout and lock controls. Evidence: cost/android-map, android-nv21-fix, android-cpu-controls and android-cpu-trace; target CPU throughput remains unqualified and carries into M6.8.
- [ ] 5c Separate Windows queue timing from producer and GPU scheduling defects.
- [x] 5d Publish platform/format/resolution limits; failed and unqualified cells explicit in PLATFORM_LIMITS.md.

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

Sole executor Evelyn, sequential only. M6 is not complete.

- M6.2a/c/d/f validated; current supported matrix14 captures, independent counts and frozen diagnostics in PRIMARY_RESULTS.md. M6.2b/e acceptance suspended: original159ms Windows event and isolated-hop/product budget evidence remain unresolved. No clean-repeat waiver.
- M6.3a Linux and fixedAndroid100/100 restartcycles in both directions passed resource gates. AndroidEOSrace repairedf8f2483, allplatform affectedtests andAndroidendurance passed. Originalcrash preserved.
- Windows3a memory slopes21.10/84.09KB/cycle fail16. RawD3Ddevicechurn reproduces growth, exclusive retaineddevice flat, Trimnegative. No implicitcache/APIchange. Resource acceptance suspended per lifecycle/WINDOWS_DEVICE_LIFETIME.md with owner/resumeconditions; no blanket attribution orwaiver.
- M6.3b functionallease/generation/EOS/pause/boundedcancellation checks passallplatforms (lifecycle/ownership andeos-fix). Sameprocess100heldlease/reconnectcycles allfunctionalpass; LinuxRSS0.142/Android-0.509KBcycle pass, Windowsprivate82.528FAILS16, handles/threadsflatall. Evidence lifecycle/lease-cycles. Windowsmemory portion remains suspended under same dependency;M6.3unchecked.
- M6.4 existingpolicy8testsperplatform pass, plusWindowsD3D11normal/death/controlledGPUwait3/3. Faultscope in fault-coverage. Linux/AndroidEGLcontrollablewait injection unavailable77, notpass; rawnegativepreserved. M6.4b/c GPU-delay subset SUSPENDED pending supportednativefenceinjection; D2publicdefaultchoice unresolved, nochange. Existingpolicies testedexplicitly, no newlyapproveddefault inferred.
- M6.5a/b/d documented;5c causal attribution suspended. M6.6a native-boundary GPU/PCM joins pass all platforms; legacy renderer300source identities/11041 repeat serials joined, physical scanout excluded. Remaining coordinated-session/current renderer join SUSPENDED pending M6.9 integrated artifact/provenance; owner Evelyn. M6.6b active: sole plaid-atlas PID1110224 three fixed Linux off/on trace pairs; status <linux-home>/dev/evidence/m6-observability/linux-trace-overhead/status.json.


M6.4a default-policy decision unresolved;M6.5–9 pending. No automatedstatus reports. Notify Jon atfullM6closure andholdbeforeM7 for maintenance.
