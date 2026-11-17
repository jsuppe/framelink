# M6 Windows readiness — approved scope and closure checklist

## Option B lifecycle gate window — Jon, 2026-11-16
For the explicit retained-device (option B) lifecycle cases only, each case runs **200 cycles and is gated on the slope over cycles 100–199**. The limits are unchanged (memory 16 KiB/cycle, handles 0.1, threads 0.02). This excludes the bounded driver copy-pool warm-up identified in [owner-attribution-20261115](lifecycle/owner-attribution-20261115/README.md). It does not change any other profile's gate, and it doesn't requalify the failed fresh-device default. The matched-100 option B results are kept as recorded.

**Result 2026-11-17 (same binaries as matched-100):** all three option B BGRA1920x1080p60 native lifecycle cases pass, giving memory/handles/threads slopes over cycles 100–199. Restart-consumer: 2.97 / −0.011 / −0.005 ([extended-200](lifecycle/device-owner-20261113/extended-200/README.md)). Restart-producer: 3.13 / −0.009 / −0.005. Held leases (`fl_read_leases --cycles 200 --device-owner`): 1.33 / 0 / 0 ([gate-200](lifecycle/device-owner-20261113/gate-200/README.md)). BGRA1080p60 native is an excluded duration cell, so this proves the lifecycle mechanism but doesn't yet cover every advertised native configuration (NV12 1080p60, NV12 3840x2160p30, BGRA 3840x2160p30).

**Advertised native configurations, 2026-11-17:** all six cases pass under the same criterion: NV12 1080p60, NV12 3840x2160p30 and BGRA 3840x2160p30, each restarting the producer and the consumer. Memory over cycles 100–199 ranges from 3.16 to 13.97 KiB/cycle, and handles/threads are falling. See [native-cells-200](lifecycle/device-owner-20261113/native-cells-200/README.md).

## Explicit retained-device owner approved — 2026-11-12 20:40:45 EST

Jon selected option B for persistent applications. Implement and qualify the explicit
selected-adapter retained-device owner described in DEVICE_LIFETIME_OPTION.md:
create/retain/release, one attached channel initially, explicit factories/bounded
producer open, fresh generation storage/fences/identity and transactional session/
GStreamer propagation. Safe references, leases and graph retirement remain mandatory;
removed devices reject. Legacy APIs/defaults remain; no hidden cache/fallback or
waiver of failed default lifecycle. Require original-limit matched100-cycle both-role
and held-lease tests, faults, compatibility and final changed-runtime qualification.
This supersedes only prior device-owner deferral/unapproved status. Other scope
exclusions, unchanged D2/D3/budgets, serial execution and M7 hold remain.
Loom: REQ-1dcc0902 (owner), REQ-2d7aec46 (preserved/amended Windows scope);
REQ-5c4df445 is superseded, not a waiver of its other constraints.

**First result 2026-11-13:** the matched 100-cycle restart-producer case passes (−0.65 KiB/cycle), and the restart-consumer case **fails** (24.93 against 16 KiB/cycle; fresh-device baseline 84.09). See [device-owner-20261113](lifecycle/device-owner-20261113/README.md). Option B is not qualified. Attribution (2026-11-15) found a bounded driver copy-pool warm-up, not a lifecycle leak. The [extended 200-cycle run](lifecycle/device-owner-20261113/extended-200/README.md) gives 22.13 for cycles 5–104 and **2.97** for cycles 100–199. Whether the gate should allow a warm-up is Jon's decision.


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
| Negotiated shared CPU | BGRA/NV12,1920x1080p60 and3840x2160p30 | Four461584b1800s cells and8/8production lifecycle cells pass | Exact artifact/revision selection and final closure remain; bounded warm100x5s lifecycle, not unrestricted cadence |
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
- [x] Shared-CPU all8target/role100cycle production controls: original resource limits pass; [summary](cpu-path/windows-cpu-lifecycle-summary/README.md).800measured cycles, exact media; this does not qualify native reconnect.
- [x] Publish a **validated bounded lifecycle** for every advertised path. Shared-CPU: 8/8 at the original window. Native: option B retained-device owner, 6/6 advertised cases plus BGRA1080 and held leases, under Jon's cycles 100–199 criterion (2026-11-16); legacy fresh-device native reconnect remains unqualified. Existing
  native persistent producer/consumer100-cycle results exceed16KiB/cycle; delayed raw-driver
  reclamation is not production equivalence. Do not advertise unrestricted reconnect.
  Option B is now approved; implement and qualify the retained-device profile under
  original limits. Do not qualify the unchanged failed default from its results.
- [x] Reconcile final renderer supported scope (conversion, ring12/pending6, exact
  revision and submission-only limits) with the transport profile. Do not claim
  two-slot renderer qualification from unrelated native transport tests.
- [x] Document existing supported-profile API/ownership/error/defaults and explicit selection/rejection in WINDOWS_APP_CONTRACT.md. Shared-CPU bounded lifecycle now measured; native lifetime contract remains a separate unchecked gate. No new default choice implied.
- [x] Record exact production source equivalence to a50841a plus the sole test-only lifecycle guard change; [scope](cpu-path/windows-cpu-lifecycle-summary/source-equivalence.json). This is not rebuild-byte equality or final release qualification.
- [x] Final exact-revision integrated validation and API/source/binary compatibility: `060c54e4` 180/180 (with owner fault test), 137 exports (128 + 9 owner), C11 headers, 3/3 old clients, hosted CI pass; [evidence](windows-integrated-final/README.md).
  Requirement: also
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
| Retained-device API | Executor | MOVED INTO M6 by explicit option B approval; implementation/qualification pending, not deferred |
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
