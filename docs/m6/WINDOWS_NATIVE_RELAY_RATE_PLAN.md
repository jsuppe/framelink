# Authenticated Windows native relay rate qualification

Status: **test-harness implemented/reviewed;28 analyzer cases pass; Windows build/runtime pending**.
See [local evidence](windows-native-relay-rate-harness/README.md).
Owner: Evelyn; native `ledger_review` owns the new harness/analyzer, root owns
review and evidence integration. Windows execution remains exclusive and must
wait for the current shared-CPU campaign to release its lane.

## Why a new harness

The existing authenticated FAR4→FAR5 three-process fixture proves functional
forwarding, PCM, terminal handling and retained ownership for eight64×32 frames.
Its per-frame parent events intentionally serialize execution and cannot prove
1080p60 or4K30 behavior. The older native boundary benchmark has pacing and GPU
completion but inherits textures and uses a different audio/control setup.
Neither is silently relabeled as this qualification.

Add a separate **test-only** paced executable using the existing local setup,
consumer snapshot, `SessionNativeRelay`, `SessionPcmRelay` and supervisor. Keep
all existing regression modes and public contracts unchanged.

## Frozen observable requirements

- Source, relay and final reader are separate processes. Texture handles are
  authenticated/pulled by FAR4/FAR5, not inherited from the harness launcher.
  Two slots, one unread frame, zero configured transport hold; no hidden fallback.
- Autonomous source pacing at30/60fps, independent100Hz PCM (480 stereo48kHz
  sample frames per block). Keep video37ms ahead of audio on the source timeline.
  Parent control events are setup/final-ownership controls, not per-frame pacing.
- Expected identities/counts derive from explicit configuration, never from the
  received set. Source payload carries a non-wrapping identity and timestamp;
  receiver verifies the payload independently of relay logs. Preserve exact
  generation, PTS, DTS, duration and PCM continuity.
- All roles record same-domain monotonic times, with overflow-safe conversion.
  Record source schedule/acquisition/upload/submit, relay acquisition/forward,
  final acquisition and actual GPU completion. Report each distribution separately.
- Validate original allocation identity at relay setup. Reader holds the genuine
  FrameLink lease through GPU completion. A held-last-frame control verifies
  upstream remains pending until actual completion; no timeout or control event
  itself may authorize early reuse.
- Real authenticated END/EOS and exact video/PCM terminal counts are mandatory.
  Publication completion and native drain are separate observations.
- Source upload and final private verification-copy/readback are explicit work,
  not zero-copy claims. The native-video relay must perform no payload map/copy/upload/readback.
  PCM transport/verification is recorded separately and is not a video zero-copy claim.
- Capture raw resource series for all three processes. Apply the existing
  steady-state second-half regression method at1800s: memory≤64KB/min,
  handles≤1/min, threads≤0.5/min. Short tests do not establish these slopes.
- Keep17ms(60fps)/34ms(30fps) whole-path arrival diagnostics. Report every miss
  and loss, including startup and drain. These are not isolated-hop budgets.
  Added-hop p95≤2ms remains **proposed**, not an approved acceptance threshold.
- Trace-off performance and explicitly bounded trace-on identity/copy accounting
  are separate runs. Trace-on must have zero dropped events/rejected threads;
  validate operation-specific joins rather than assuming absent records mean
  zero operations. Do not size every recorder to an unbounded endurance run.
- Failure must preserve partial evidence and terminate only owned child processes.
  A missing/truncated record, malformed footer or failed child is a failure,
  never an implicit skip or a reason to select a more favorable repeat.

## Validation order

1. Local analyzer negatives: missing/duplicate identity, changed PCM/PTS,
   premature completion, truncated data, trace loss, nonfinite/degenerate
   resource series. Include valid controls to prove usable schemas.
2. Focused Windows build and tiny-geometry functional smoke, retaining genuine
   authenticated peers, GPU completion, last-reader hold and terminal handling.
3. One target-resolution five-second trace-off preflight after review.
4. Matched short trace-on identity/copy accounting with explicit recorder capacity.
5. One1800s trace-off target cell only when its earlier gates pass. Preserve
   failures and investigate a concrete mechanism before another attempt.

No result here establishes physical scanout/acoustic alignment, arbitrary decoder
compatibility, real-decoder two-hop performance, other platforms, API freeze or
M6 completion. Existing startup/native-lifetime failures remain suspended rather
than waived. Product D2/D3 and isolated performance-budget decisions remain Jon's.
