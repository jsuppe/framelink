# Review option: explicit Windows device lifetime

Status: **proposal only; not approved, implemented, exported or qualified**.
Prepared2026-11-12 after the [causal controls](device-causal-controls-20261112/README.md).
This makes the previously identified product decision reviewable. It does not
change D2/D3, the public default, or any failed acceptance gate.

## Decision boundary

Choose whether to support an **additional opt-in application-owned device-lifetime
profile**, retaining existing fresh-device factories unchanged. It may reduce
churn, but a successful opt-in profile would not qualify or silently replace the
failed default profile. Whether that additional profile satisfies the product's
required deployment contract is Jon's decision. Keeping the existing requirement
instead requires a backend/driver remedy and matched default-profile measurements.

Evidence supports studying this option, not claiming it will pass: the prior
exclusive retained-device control was flat; new hardware churn controls show
substantial deferred reclamation. No production two-process/profile evidence
exists for this proposed lifetime. It must pass original limits on its own merits.

## Proposed ownership contract

- Caller explicitly creates a device-owner object on a selected adapter, retains
  it across **sequential** channel lifetimes, then releases it outside DLL unload.
  No global cache, static-destructor GPU cleanup, hidden retention or singleton.
- Library creates the device with the existing feature-level/multithread settings.
  Do not accept arbitrary externally supplied devices in the first version: their
  immediate-context ordering and multithread protection are not established.
- At most one attached channel per owner initially. A second attachment returns
  busy rather than silently sharing a queue between independent live channels.
  Different owners can still support independently owned simultaneous channels.
- A channel retains its owner; releasing the caller's reference does not destroy
  an attached device. No unbalanced COM-count polling to infer ownership.
- Reuse only device/context identity. Every channel generation receives fresh
  fences, ring ownership, metadata and tokens. Never reuse old-generation storage
  solely because the device survived. Close does not assert GPU completion.
- Channel attachment is serialized with owner destruction. Adapter/LUID mismatch,
  removed-device state or unavailable required fence/context capability rejects
  attachment; never silently choose another GPU or a CPU fallback.
- A removed owner cannot be revived for another generation. Explicit replacement
  is required; all old leases remain governed by their original failure contract.
- Native device references and external GPU work remain caller obligations.
  Document how retained COM references outlive the library owner's reference and
  prohibit claiming final GPU teardown merely because the library reference drops.
  No owner reuse while application work can mutate the old channel's resources.
- Snapshot and GstMemory lifetime must be carried through the graph's existing
  ownership, not replaced by the owner. Outstanding graph-owned references must
  prevent declaring the old channel retired. Dead-peer shutdown cannot invent
  successful lease completion or release unsafe old allocations into a new ring.

## API surface required before implementation approval

Use additive opaque-owner create/retain/release operations and explicit variants
of consumer creation, external-pool consumer creation and bounded producer open.
Names/signatures remain provisional pending ownership review; do not add symbols
from this document alone. Existing native symbols/layouts and legacy behavior
remain untouched. Unbounded legacy producer open is not an acceptable shortcut.

A session configuration must carry a retained owner for the appropriate local
role through authenticated setup and unwind it transactionally on cancellation.
GStreamer setup must carry that same lifetime across reconnect without keeping
stale pads, sessions, snapshots or frame leases. The bin must not manufacture a
process-global owner implicitly. Raw-only success is not session/bin integration.

## Required review and acceptance before a supported profile

1. Confirm whether opt-in support is useful if the default profile remains failed.
2. Review owner/channel/graph retirement state machine, including external COM/GPU
   references and snapshot/lease survival. Reject ambiguous safe-reuse inference.
3. C11/header/export compatibility and unchanged clients; negative adapter,
   removed-device, busy attachment and cancelled-setup checks.
4. Outstanding-reader, downstream-death, rollback and reconnect generation tests
   retain real fences/ownership and exact pixels/PCM. No synthetic drain.
5. Matched100-cycle producer/consumer and held-lease profiles using original
   private-commit/handle/thread thresholds, separately labeling opt-in and default.
6. Required target timing qualification for the integrated changed runtime. Prior
   a50841a passes cannot qualify new DLL bytes. Preserve suspended timing failures.

## Explicit non-solutions

No sleep inserted to allow delayed cleanup before sampling, no working-set purge,
no Trim-based claim (already negative), no ignored outstanding references, no
waiver based on COM refs0, no hidden native-to-CPU fallback, no D2/D3 default change,
and no claim that this proposal closes M6 or releases M7.
