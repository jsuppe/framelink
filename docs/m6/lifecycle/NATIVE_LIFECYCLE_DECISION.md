# Native lifecycle: remaining product boundary, not a qualified profile

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


As of2026-11-13: shared-CPU lifecycle is8/8passed. Native persistent-role100-cycle
private-commit slopes remain21.10/84.09KiB per cycle; held-lease profile82.528,
against the unchanged16KiB limit. Raw device-only deferred reclamation does not
qualify production and does not justify sleep/Trim/cache or equivalent clean reruns.

M6's approved narrower scope still requires a validated lifecycle for advertised
native paths. Neither the broad rescope nor passing shared CPU selects a new
native ownership contract. Existing native APIs remain implemented; unlimited
in-process reconnect is NOT QUALIFIED. No default/API/budget is changed here.

## Concrete choices

### A. Evaluate a fresh-worker, single-native-generation support profile

Keep existing APIs and production code. Every participating source/relay/final
worker process owns exactly one graph generation and exits after real ordered
completion. No second native channel/generation is created in that worker.
All borrowed COM/native handles, snapshots and GPU leases belong to that worker
group; no live references may silently escape into an application process that
is claimed retired. A fresh graph uses new processes, identities and allocations.

Normal close requires genuine releases/drain and GPU completion. On failure,
quarantine old resources; cancellation/death/worker exit never counts as successful
frame completion. No slot/epoch is recycled into the next graph. Preserve all
existing fault and held-reader rules; a worker supervisor is not a device cache.

This is a **proposed support restriction**, not a measured replacement for the
failed persistent-process profile. Before advertising it, define/freeze accounting
for all worker/driver resources across exits, then qualify repeated actual native
three-process graphs at the named targets with exact pixels/PCM/identities,
bounded faults, original timing limits and no hidden retained resources. Do not
substitute a flat empty supervisor's memory for native worker/driver accounting.
Existing1800s single-graph passes are relevant evidence, not proof of that complete
lifecycle. Jon must accept this deployment restriction; executor owns scoped
implementation/measurement after the contract is settled.

### B. Require repeated native sessions within a persistent application

The [reviewed explicit device-owner option](DEVICE_LIFETIME_OPTION.md) is the
concrete additive proposal: selected adapter, one attached channel initially,
fresh per-generation storage/fences/identity, retained owner/reference semantics,
no removed-device resurrection and full session/GStreamer propagation. It received explicit implementation authorization at20:40:45EST. It must pass original100-cycle limits and
ownership/fault tests; it cannot qualify the unchanged fresh-device default.

### C. Defer advertised native readiness while preserving its implementation

Initial advertised readiness would be the qualified explicit shared-CPU matrix,
with native/renderer GPU paths still experimental/not-qualified for the unresolved
lifecycle. This is a further support-scope choice for Jon, not inferred from the
approval that excluded only native BGRA1080 and decoder4K. A later driver/backend
remedy or approved profile reopens native readiness with its own gates.

## Recommendation and execution boundary

If applications can accept worker isolation, review A first: it avoids a new API
and makes the process boundary explicit, but still needs qualification and may
not meet the application's persistent-process requirement. If that requirement
is essential, B is the relevant reviewed option, not an implicit cache patch.
Do not relabel the current native lifecycle as passed under either choice.

No further equivalent native duration/device-churn rerun is justified by current
evidence. Final supported artifact selection, integrated/API reconciliation and
verified merge follow the chosen contract; prior exacta50841a178/178/128exports/
3unchanged-client results remain preserved, not silently upgraded into release
readiness. M6OPEN; M7HELD. Owner: Jon for contract/scope, executor for concrete
validation/repair, platform/driver for default-profile remedy if that is retained.
