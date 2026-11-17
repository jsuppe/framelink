# Windows remaining execution dependencies — 2026-11-12

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


## Approved M6 Windows readiness scope — 2026-11-12 18:14 EST

Jon approved readiness for explicitly named Windows configurations, not universal
Windows support. This supersedes older broad M6 completion requirements below;
historical results and failed gates remain evidence, not silently passed tasks.
Authoritative reviewable scope/checklist: [M6 Windows readiness](M6_WINDOWS_READINESS.md).
M6 remains OPEN; M7 maintenance hold remains. Correct media/timeline/ownership,
fault handling, validated bounded lifecycle and final exact-revision integrated/API
compatibility gates remain mandatory. No new budget, D2/D3 default or device-owner
API approval is inferred. Failed native BGRA1080 and decoder4K are NOT QUALIFIED
and excluded from initial advertised support. One task and one executor, serially.


All four frozen461584b shared-CPU duration cells and all8target/role production lifecycle cells are collected passes; Windows runtime is idle after01:23:12ZNovember13. Exacta50841a178/178,
128exports and3 unchanged clients are already collected; do not repeat them.
Controlled Windows GPU-delay, existing-policy metadata/audio faults, held-reader,
pause/death/cancellation functional checks already exist in that suite.

## Resolved dependency: current renderer source selection

M6.6a/9d current-renderer integration is distinct from the legacy presentation
probe. Existing evidence explicitly uses100ms scheduling and no active display;
it proves submission identities, not current primary-mode acceptance or scanout.
Read-only source lookup on November12 found:

- <win-home>/dev/vkrender-m5-final-validation has the historical av_input.h /
  av_schedule.h source and built renderer, but no Git repository identity.
- <win-home>/dev/vkrender is ed03cb8466d9010d2861dc51c6a65790dd3853c7 and
  lacks that av_input.h path; an unrelated untracked file is preserved.
- <linux-home>/vkrender-src is983d1ea with unrelated dirty tracked/untracked
  work. No edits made. The local bare mirror masterc71e013 is not the historical
  M5 source. Searching local available files found no av_input.h/av_schedule.h.

Do not silently replace an unrelated current checkout, claim the old binary is
current, or choose a new renderer architecture from these different baselines.
**Resolved by approved plans and GitHub PR13:** merged9f74b9d is the existing
approved baseline. Clean isolated source and137-file historical comparison are
[pinned here](renderer-provenance-9f74b9d/README.md). No new Jon architecture
choice is needed. Isolated integration is now collected: renderer repaira6a1906,
4focused tests,9CLI controls and three complete5s source-to-submission captures pass.
See [integration evidence](renderer-integration-a6a1906/README.md). No physical-output
claim, new performance budget or renderer endurance qualification is inferred.

## Other dependencies, owners and resume conditions

| Item | Owner | Resume condition |
| --- | --- | --- |
| Supported bounded lifecycle | Evelyn: evidence/contract reconciliation; Jon: any changed supported contract | Existing100-cycle native growth fails. Option B is approved; executor owns additive implementation and original-limit qualification. No cache/sleep workaround or implicit waiver |
| D2 overload / D3 convenience fallback | Jon only if introducing new policy | New convenience defaults deferred. Existing explicit profiles/defaults remain and routine supported-contract documentation is ready, not blocked |
| Native BGRA1080 natural323ms stall | Evelyn causal investigation | Original-scale phase/thread/I/O evidence localizing the natural delay; synthetic pause proves catch-up mechanism only. No uninstrumented duration retry |
| Decoder4K misses | Evelyn causal investigation | Evidence localizing original source-oracle/timing outlier, which isolated phase control did not reproduce; no clean-repeat waiver or decoder endurance |
| Historical startup/159ms native timing | Evelyn causal investigation | Matched original-scale phase/wait evidence plus compatible remedy, not another equivalent clean control |
| Default-profile lifecycle slopes | Evelyn/backend/driver; Jon only for contract changes | Real remedy plus unchanged matched production gates. New deferred-reclaim probes do not qualify default profile |
| Renderer supported-profile acceptance | Evelyn; Jon only for changed support/budget | Current0/100ms NV12 submission proof uses ring12/pending6 and explicit CPU conversion. No two-slot/endurance/physical-output inference; reconcile named intended support with existing budgets |
| Linux/Android integration and campaigns | Project scope/Jon | Windows-first deferral lifted or required focused shared-code check; no broad campaign is authorized as a workaround for Windows blockers |
| Final M6 closure/CI/API freeze | Evelyn after predecessors | Resolved applicable gates/decisions and final exact changed-runtime qualification; M7 remains held |

No additional supported Windows missing-cell duration or unrun existing-policy
functional check was identified in this reconciliation. This does not assert
that all project work is mathematically impossible. Renderer integration is collected;
remaining bounded-lifecycle and final supported-profile gates are explicit in
[M6 Windows readiness](M6_WINDOWS_READINESS.md). Do not launch speculative repeats merely to keep a process running.

## Remaining native contract choice and closure dependencies

[Concrete native lifecycle choices](lifecycle/NATIVE_LIFECYCLE_DECISION.md): accepted
fresh-worker/single-generation support with matched qualification, explicitly
authorized device-owner implementation for persistent applications, or a further
deferral of advertised native readiness. Jon selected B at20:40:45EST; earlier alternatives below are historical, not pending approval.
Jon owns that support contract; executor owns validation/repair after it is settled.
Final release artifact selection/integrated/API gates and verified merge depend
on that boundary. Source reconciliation finds113production/header/runtime/sample/
CMake blobs identical toa50841a; only the test-only lifecycle guard differs.
Next action is approved option B implementation, not a repeat of unchanged native failures.
