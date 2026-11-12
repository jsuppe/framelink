# M6 Windows readiness — approved scope and closure checklist

Approved by Jon on 2026-11-12 at18:14EST; Loom requirement `REQ-5c4df445`.
**M6 OPEN / API NOT FROZEN / M7 HELD.** This is a candidate matrix, not a release
support declaration. Narrowing scope does not relax correctness or memory gates.

## Exact environment and candidate support

Only measured host **<win-host>**, Microsoft Windows11 Home **10.0.26300/build26300**,
**NVIDIA GeForce RTX5090, driver32.0.16.1714**, same-adapter operation is in scope.
No inference for another driver, GPU, adapter pairing, OS build or format/rate.
FrameLink integrated candidate `a50841aad44fbc9431234a4f326bad036d193bf5`;
DLL source lineage `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`.
Renderer approved baseline `9f74b9d1fae7e13d520bc5d4ae9827ac568704f6`, focused
explicit-zero repair `a6a19061360761729db41bfa427f364037985b1a`.
Final publication must pin the selected exact integrated revisions, not floating HEAD.

| Explicit path | Candidate configuration | Existing evidence | Readiness boundary |
|---|---|---|---|
| Negotiated shared CPU | BGRA/NV12,1920x1080p60 and3840x2160p30 | Four461584b1800s cells pass | Final revision equivalence and accepted lifecycle still required |
| Native same-adapter two-hop | NV12,1920x1080p60 and3840x2160p30; BGRA3840x2160p30 | Threea50841a1800s cells pass | Strict ownership/synchronization; bounded lifecycle still required |
| Native same-adapter two-hop | BGRA1920x1080p60 | Finala50841a run FAILED after~695s | **NOT QUALIFIED; excluded**. Earlier11d1b65 pass does not override failure |
| Real decoded source relay | 1920x1080p60 | Matched short pair pass77afa1a | Functional/short-rate evidence only; no endurance support claim |
| Real decoded source relay | 3840x2160p30 | Replay trace-on two34ms misses, max36.6673ms | **NOT QUALIFIED; excluded** |
| Renderer final submission | NV121920x1080p60, explicit0ms or100ms, native input, ring12/pending6 | Three5s complete-trace captures,300identities/240000PCM each | Submission/control proof only, not endurance, small-pool qualification or physical output; explicit CPU NV12 conversion, not end-to-end zero-copy |

The first two rows are candidate advertised transport paths after all remaining
gates; decoder/renderer limited evidence must not be presented as broader support.
Profiles remain explicit: reject incompatible native layout/adapter/allocation;
no hidden copy fallback. Existing entrypoint overload defaults are retained and
must be documented; no new uniform D2/D3 default is selected.

## Mandatory closure checklist — executor owns verification

- [x] Recover approved renderer lineage and preserve unrelated dirty trees.
- [x] Focused integrated renderer build;4/4 scheduler/clock/pacer/NV12 checks.
- [x] Explicit0..2000ms renderer option with9CLI boundary controls; default100ms unchanged.
- [x] Newly integrated NV12 renderer source/acquire/adopt/render/GPU/present identity,
  PCM and trace-loss checks at0ms and100ms. Original undersized trace capture retained as failed.
- [x] Existinga50841a integrated178/178,128exports retaining119,3unchangedclients archived.
- [x] Named shared-CPU and three passing native duration cells archived; failed cells retained.
- [ ] Publish a **validated bounded lifecycle** for every advertised path. Existing
  persistent producer/consumer100-cycle results exceed16KiB/cycle; delayed raw-driver
  reclamation is not production equivalence. Do not advertise unrestricted reconnect.
  Executor must validate an authorized existing contract or bring a concrete contract
  choice to Jon. Optional device-owner proposal is not implementation authorization.
- [ ] Reconcile final renderer supported scope (conversion, ring12/pending6, exact
  revision and submission-only limits) with the transport profile. Do not claim
  two-slot renderer qualification from unrelated native transport tests.
- [ ] Finish supported-profile API/ownership/error/default documentation, explicit
  selection/rejection, and supported-lifecycle integration. No new default choice implied.
- [ ] Final exact-revision integrated validation and API/source/binary compatibility
  reconciliation after planned supported-path changes. Reuse exact unchanged evidence
  only with explicit hash/source equivalence; no unnecessary campaign repetition.
- [ ] Final evidence review: every support row has command/settings/revision/host,
  raw outcomes and exclusions; no suspended failure is counted as pass.
- [ ] Record M6 readiness only after all applicable gates; notify Jon and **HOLD before M7**.

## Deferred expansion backlog — separate from M6 readiness

| Work | Owner | Resume condition |
|---|---|---|
| Broad Linux/Android performance/integration | Executor; Jon prioritizes | Explicit post-M6 expansion authorization; retain existing failed CPU/Android cells |
| Other GPUs/drivers/cross-adapter/formats/rates | Executor; Jon selects matrix | Named expanded matrix and original budgets before qualification |
| Native BGRA1080 and decoder4K failures | Executor | Causal evidence and justified repair, then affected original gates; no speculative duration repeats or waiver |
| Optional retained-device API | Jon contract decision; executor implementation | Required by accepted lifecycle and explicitly approved API/ownership contract; otherwise deferred |
| Automatic fallback/convenience profiles | Jon policy; executor implementation | Explicit behavior/default approval; explicit selection/rejection remains in M6 |
| Physical display/acoustic timing | Jon scope/budget; executor measurement | Approved physical-output methodology and budgets; submission timestamps are not scanout |
| Packaging/release/developer handoff | Executor after Jon maintenance release | M6 closure plus explicit release of M7 hold |

Deferred work is not silently completed or discarded. D2/D3 product choices remain
Jon-owned where a new default is desired, but preserving documented existing explicit
profiles does not block routine supported-path implementation.

## Evidence

- [Current renderer integration](renderer-integration-a6a1906/README.md)
- [Detailed ledger](LOW_LATENCY_PLAN.md)
- [Platform evidence including historical excluded configurations](PLATFORM_LIMITS.md)
- [Device-lifetime evidence and unresolved contract](lifecycle/WINDOWS_DEVICE_LIFETIME.md)
