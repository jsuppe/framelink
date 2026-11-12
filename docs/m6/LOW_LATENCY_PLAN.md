# M6 low-latency and native-memory resolution plan

Revision 1 — 2026-11-04. Owner: project executor. Independent work may run in
parallel under the November 12 override; Windows runtime has one exclusive owner.
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
Decision needed: realtime overload behavior and API default. Preserve existing
entrypoint behavior until resolved; do not change it just to pass. API audit
2026-11-09 found the prior “current fail-fast” wording overbroad: thin native
copy-in defaults error-on-ring-full=false (drop-and-count); strict profiles opt
into failure. No uniform default or product decision is inferred. Exact observed
defaults and remaining property gaps: windows-api-candidate/README.md.

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
trace state, all failures, and raw evidence. Independent non-overlapping work may run concurrently under Jon’s2026-11-12 authorization; Windows runtime campaigns retain one exclusive owner.

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
- [x] 5a Measure software call-boundary copy/lock/wait/convert/present costs and resource growth. Evidence/scope: cost/STAGE_COSTS.md. GPU execution, physical scanout and primary-renderer qualification are not inferred; remaining gates stay open.
- [x] 5b Isolate Android NV12 CPU costs with format/layout and lock controls. Evidence: cost/android-map, android-nv21-fix, android-cpu-controls and android-cpu-trace; target CPU throughput remains unqualified and carries into M6.8.
- [ ] 5c Separate Windows queue timing from producer and GPU scheduling defects.
- [x] 5d Publish platform/format/resolution limits; failed and unqualified cells explicit in PLATFORM_LIMITS.md.

### M6.6 — Observability without hidden overhead
- [ ] 6a Join independent source identities through each boundary, GPU completion
      and presentation submission; separately track PCM and A/V offset.
- [x] 6b Compare trace off/on, quantify memory per recorder and loss detection; evidence observability/TRACE_OVERHEAD.md.
      Default reduced to4096events/thread; explicit sizing and overflow reporting validated on all platforms.

### M6.7 — Repeatable automation (closure infrastructure)
- [x] 7a Add deterministic queue/ownership/timing negatives to CI. Hosted CPU trace checks and opt-in native hardware selection updated; local Linux19pass1skip/Windows20pass evidence in automation. HostedActions execution is not claimed; final integrated CI remains7c.
- [x] 7b Publish reproducible real-device scripts and machine-readable gates. tools/m6_validation.py validated on Linux/Android/Windows with explicit skipped/missing/failed accounting; automation/RUNNING_GATES.md.
- [ ] 7c Ensure final acceptance references the exact integrated revision after
      M6.8/M6.9; old benchmark passes are not final qualification.

### M6.8 — CPU-consumer memory path repair
- [x] 8a Reproduce Linux slow/bimodal readback with allocation/cache diagnostics. Evidence: [CPU-path investigation](cpu-path/README.md); no production repair/target-rate claim.
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

### Execution-order override — Jon, 2026-11-12
Preserve and collect the already-running exact 7b8a274 integrated campaign; do not cancel or repeat it. Thereafter complete remaining Windows implementation and API-contract work sequentially, using builds and narrowly targeted checks for immediate defects. Batch full integrated suites, broad fault/endurance/performance campaigns and final compatibility qualification after planned Windows code/API changes. Fix validation failures and qualify the final exact revision before readiness claims. Code complete is not validated/API frozen/M6 complete. Linux/Android broad campaigns remain deferred; M7 hold unchanged.

## Priority override — Jon, 2026-11-08 16:29 EST
Jon requests completing all remaining Windows-specific changes first and stabilizing the API. This supersedes the previous Linux/Android refresh order. Preserve and collect the currently running Linux BGRA1080p60 test (already near completion), then do NOT launch the queued Linux NV12 refresh. Defer additional Linux/Android campaigns except focused checks required by shared API/code changes.

Next, inventory and execute remaining Windows M6 implementation and repairs sequentially: shared-CPU allocator/transport integration; Windows D3D11 native-producer integration into coordinated sessions (M6.9); reconnect/device lifetime memory growth; outstanding Windows timing/queue defects and required fault handling. Validate each change with focused Windows checks. Track genuine blocked items with owner/evidence/resume condition, not silent acceptance.

Make API stabilization an explicit deliverable alongside this Windows-first work: audit native C/platform APIs and GStreamer properties/session contracts, resolve overload/fallback defaults, preserve compatibility or document versioned migration, and check planned Linux/Android integration will not require breaking the proposed contract. Publish an exact revision and compatibility checks for an app-integration baseline. Do not claim the API is frozen until reviewed/tested; do not equate a Windows-ready API baseline with full cross-platform M6 completion. Bring forward API stabilization work, not unrelated M7 packaging/release; retain maintenance hold before M7.


Parallel independent work is authorized (Jon, 2026-11-12). Keep non-overlapping source ownership and one Windows runtime executor. M6 is not complete.

### Authoritative execution state — 2026-11-12

API candidate: **`461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`, NOT FROZEN**.
Runtime DLL sources remain unchanged from the candidate. Checked relay sample
fix `462fa5f` is newer: it avoids an unnecessary acquisition after real native
drain; eight focused Windows tests passed. It still requires final integration.
M6 remains open; M7 remains held.

| Work | Concrete state and evidence | Owner / next action |
| --- | --- | --- |
| Windows relay implementation | Validated: source-relative input, safe consumer-pool snapshot, FAR5 same-allocation output, actual three-process native/PCM, held-reader replacement/source-death, explicit automatic receiver and transactional unpublished setup | Evelyn; implementation complete within identified Windows scope; no invented convenience API |
| Integrated Windows/API | Clean candidate build/API + **169/169 tests in655.72s**, no skips;119 prior export names retained +9 additive =128. [Evidence](windows-integrated-461584b/README.md) | Collected; do not repeat diagnostics already in CTest |
| Old-client compatibility | **3/3 unchanged7b8a274-built clients** pass against baseline and candidate DLLs; exact binary hashes verified. Source review found no existing public layout/enum/signature break. [Evidence](windows-compat-461584b/README.md) | Collected; bounded proof, not all-version/ordinal ABI freeze |
| Windows sharedCPU NV12 1080p60 | **PASS** exact461584b: one1800s run,108000video/86400000PCM/180000audio; zero17ms misses, inferred A/V delta0ns; memory7.233/7.569KB/min below64, handle/thread gates pass. [Evidence](cpu-path/windows-cpu-nv12-1080p60-461584b/README.md) | Collected and independently reanalyzed; four binary hashes unchanged. Runtime released06:54:44Z; do not repeat |
| Windows other sharedCPU targets | **BGRA4K30 PASS** exact461584b:1800s,54000video/86400000PCM/180000audio, zero34ms misses; memory7.166/7.389KB/min, handle/thread gates pass. [Evidence](cpu-path/windows-cpu-bgra4k30-461584b/README.md). Older76a65d2 BGRA1080p60 pass retained; NV124K30 unqualified | Collected and independently reanalyzed;58 original hashes verified. Runtime released07:25:52Z; do not repeat |
| Native two-hop qualification harness | All4 BGRA/NV12 1080p60/4K30 matched5s trace-off/trace-on profiles **PASS** at11d1b65, exact video/PCM and zero17/34ms misses; both trace boundaries joined without relay payload copies. [Verified targets](windows-native-relay-targets-11d1b65/README.md). Initial MSVC and collection separator failures preserved | BGRA1080p60 native1800s **PASS**:108000video/86400000PCM, zero17ms misses; memory slopes0/0.400218/0KB per minute and handle/thread gates pass. Root independent raw analysis passed; [duration evidence](windows-native-relay-duration-bgra1080-11d1b65/README.md) archived. Remaining three native duration cells unqualified |
| Authenticated real-decoder relay qualification | New4d38a09 tiny real-decoder FAR4→FAR5 fixture clean Windows build/test/analysis **PASS**:16 distinct decoded frames,25600PCM,37ms offset and held-reader proof;7 checker cases/1 local CTest pass | Collected and independently checked; [evidence archived](windows-local-decoder-4d38a09/README.md). Explicit conversion/source-final readbacks, not target-rate or direct decoder-zero-copy proof |
| Paced real-decoder relay | Corrected CNG full-SHA256 harness at77afa1a: clean build,9/9 focused tests,2s smoke and both5s1080p60 trace arms **PASS** (300distinct/240000PCM each, zero17ms misses). Initial GLib failure retained. 4K30 preflight **FAILED**:150distinct/240000PCM exact,140video34ms misses; decoder-pull p5022.0109ms and source oracle13.6742ms | Root; local causal investigation of on-demand fixture generation/encoding versus decode. No 4K trace-on or decoder endurance run; full oracle and gates retained |
| Automatic FAR5 native receiver pause/resume | **PASS** after462fa5f checked-sample fix: collect only missing reservations, no post-drain extra acquire. Windows8/8 focused tests passed10.08s, including original pause case and genuine outstanding-reader death | Root; [initial failure](windows-native-pause-validation/initial-failure-9ea51d4/README.md), [diagnostics](windows-native-pause-validation/diagnostics/README.md), [fix evidence](windows-native-pause-validation/fix-462fa5f/README.md) archived. Real ownership/error checks retained |
| Historical Windows startup | **Suspended, not waived.** Original1529.4514ms is source-to-receive, not proven producer-open time. New buffered phases show current158–164ms opens dominated by device creation; legacy94/100 vs coordinated100/100, exactPCM, no reproduction. [Evidence](windows-startup-phase-461584b/README.md) | Evelyn; resume on original-scale phase/wait evidence plus compatible remedy, not equivalent clean repeats |
| Windows native lifetime | **Suspended, not waived:**21.10/84.09KiB/cycle exceed16KB/cycle. Raw device churn/reuse/Trim controls identify a contribution, not complete attribution. [Contract](lifecycle/WINDOWS_DEVICE_LIFETIME.md) | Evelyn owns investigation; driver/backend remedy or explicitly approved device-lifetime contract required. Jon owns contract/acceptance changes |
| D2 / D3 | Unresolved public overload/convenience-fallback decisions; existing explicit profiles/defaults preserved | Jon; no automatic default change or hidden native-to-CPU fallback |
| Linux / Android campaigns | Broad work deferred under Windows-first direction. Prior Linux BGRA and other completed campaigns collected, not repeated. Android BGRA4K and NV121080 residual failures remain suspended; clean trace diagnostics did not waive them | Evelyn; resume deferred integration only after Windows priority permits; causal fixes required for suspended failed cells |

**Windows runtime owner:** root; idle verified08:13:24.7934383Z after the
c539d99 scheduling control. Clean build,9/9 focused tests and tiny decoder smoke
passed. The bounded two-AU encoded queue was insufficient:4K30 delivered all150
distinct frames/240000PCM, but118 video arrivals exceeded34ms (max542.183067ms),
with no audio misses. Source decoder-pull median21.7833ms remains the largest
phase. [Control evidence](windows-decoder-queue-c539d99/README.md) preserves
successes and failure; trace-on and endurance did not run.

Next: bounded measurement of on-demand fixture generation and encoding
throughput before asserting a decoder or transport cause. No repeated target
without a concrete discriminating control. Existing queue limit is only for
resident compressed AUs, not all pipeline buffers or a total byte cap.
The final integrated/API/unchanged-old-client runner is prepared and reviewed,
including per-DLL export sets and full collection/compatibility verification;
20 local validator tests passed. No full integration has run on the newer helper.

CNG comparison preserved identical full SHA256 across120 samples/known vectors;
1080p hash median18.05→3.24ms,4K72.68→12.95ms. Initial unavailable MAXULONG
build failure preserved; typed-limit correction77afa1a passed9/9 tests.
[Corrected1080p and failed4K evidence](windows-decoder-cng-77afa1a/README.md)
is archived with134 independently verified committed blob hashes. Original
native BGRA duration remains valid for its pinned11d1b65 scope, not final
qualification of the newer checked relay helper. M7 held.

### Qualification exclusions retained

- Current functional relay/decoder/PCM checks do not establish final-rate native
  two-hop performance, physical presentation or complete all-platform M6.9d/e.
- Original Windows native159ms stall and low-latency product/isolated-hop budgets
  remain unresolved; old sustained passes are not clean-repeat waivers.
- Linux/Android EGL controlled GPU-delay injection was unavailable (77), not a
  pass. Supported Windows faults and ordinary completion/death do not fill it.
- SharedCPU copies are explicit; same-allocation relay proof is separate from
  CPU-readback or timing-oracle work. Snapshot lifetime never substitutes for a
  real reader lease or GPU completion.
- Final cross-platform/CI/merge gates and D2/D3 remain open. No automatic status
  reporting is re-enabled; silent execution recovery remains authorized.

Historical per-step receipts, failed controls and exact prior counts are preserved
in [the execution archive](LEDGER_HISTORY_20261112.md) and
[earlier relay handoffs](windows-lease-relay/prior-ledger.md). Those snapshots do
not supersede this current state. Notify Jon at verified full M6 closure and hold
before M7 maintenance.
