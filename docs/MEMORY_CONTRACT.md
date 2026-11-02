# M5a.1 — optional memory/usage policy contract v1

This implements the portable **policy layer**, not platform discovery, allocation,
GStreamer native-memory support or runtime zero-copy qualification. Public API:
`include/framelink_memory.h`; implementation: `src/framelink_memory.cpp`.

## Compatibility and deployment

- Existing `FL_VERSION=6`, A/V session version1, 160-byte session messages and
  legacy channel APIs remain unchanged. Existing clients do not acquire a new
  strict policy or change pool size, pacing, PTS or allocation flags.
- New clients explicitly negotiate support for this separate extension before
  committing allocations. Missing support or a different version cannot satisfy
  a strict request. An application may explicitly choose its legacy non-strict
  mode, but must not label that downgrade zero-copy or silently discard needs.
- Requirement messages belong inside the authenticated session/generation's
  control exchange. They contain policy, not authentication, handles, allocation
  identity or a new clock. They must not be appended to the legacy session wire.
- These pure helpers do not themselves exchange/authenticate messages or change
  runtime paths. The implemented authenticated provider/GStreamer integration is
  documented separately in GST_NATIVE_MEMORY.md; callers must opt into it.

## Required and preferred usage

Usage bits: CPU read, CPU write, GPU sample, GPU render and decoder output.
`requiredUsage` is a conjunction: every requested bit must be supported.
`preferredUsage` reports unmet wishes but does not relax any hard requirement.
Overlap is allowed; a missing required bit still rejects the path even when it
also occurs in preferences. Zero usage demands is legal, not an inferred usage.

`allowedResourceTypes` is a nonempty mask of shared CPU, D3D11, DMA-BUF or
AHardwareBuffer resource types. A candidate path identifies exactly one type.
Resource/API identity does not establish physical VRAM/system-memory residency.
The enum's existence is not a claim that a backend implements that allocation.

Producer and consumer needs are distinct. For decoder output → sampled image,
producer decoder-output support and consumer sampling support must both pass on
the **same compatible allocation**. Do not intersect usage masks and accidentally
erase a hard requirement. Conflicting requirements are not resolved by global
preference. Allocation selection/device/layout/ownership compatibility belongs
to platform adapters and subsequent negotiation tasks.

## Explicit boundary operations

`allowedOperations` permits any combination of pixel copy, readback, upload and
format conversion. Zero is strict: none of those boundary payload operations
is permitted. `flMemoryRequirementsInit` explicitly initializes that policy,
with any known resource allowed and no usage demands. This initializer is opt-in,
not the behavior of existing channel/session constructors.

Adapters report **all** operations incurred by their offered path. Allowing
readback alone does not also permit a separately reported copy or conversion.
Handle duplication, descriptor transfer and fence exchange are not pixel copies.
A required processing transformation may produce new output, but must not conceal
an extra copy solely to cross a boundary. Conversion at the final renderer must
also be described when making end-to-end zero-copy claims.

CPU readability can be direct or staged depending on the actual path. Reporting
CPU-read support does not override strict rejection of a required readback.
GPU-resource identity alone never proves zero-copy.

## Candidate policy evaluation

`flMemoryPath` is a declaration for a specific endpoint and exact agreed format,
planes/layout, device, ownership and synchronization arrangement—not a global
device capability. Platform adapters must establish those facts before offering
it. The structure is not a native-struct wire format.

`flMemoryEvaluate` checks one requirement against one candidate. Evaluate both
endpoints before agreement. It reports missing required usage, unmet preferences,
rejected resource type and forbidden operations. Missing preferences alone is
successful; any other diagnostic makes the result incompatible.

Success establishes policy compatibility of the **declarations only**. It neither
detects lying/incomplete adapter reports nor proves physical residency, shared
allocation identity, proper fence lifetime, throughput or measured copy count.
Those are separate M5a implementation/validation gates. No allocation or payload
operation occurs in this helper. It does not choose among multiple candidates.

Unknown flags, zero allowed resource mask, nonsingleton candidate type, nonzero
reserved fields and unknown versions are rejected. On invalid/version errors,
output stays unchanged. OK/incompatible results publish all policy diagnostics.

## Requirements wire message

Exactly64 bytes; unsigned little-endian32-bit fields, no native-struct packing:

```text
 0.. 3  magic "FMR1"
 4.. 7  memory contract version = 1
 8..11  requiredUsage
12..15  preferredUsage
16..19  allowedResourceTypes
20..23  allowedOperations
24..63  reserved, all zero
```

Encode/decode rejects other lengths, magic, versions, unknown bits and reserved
data without partially changing output. Struct reserved fields must also be zero.
Future semantic changes require an explicitly understood contract version; old
decoders do not silently ignore unknown preferences or permissions. Existing
session connect/idle deadlines remain the enclosing exchange's responsibility.

No PTS or buffering field exists here. Media timing remains on the existing
common A/V timeline; native buffer readiness is a separate synchronization fact.

## Task acceptance

- Real exported-library C++ and C11 callers validate usage/policy behavior.
- Independent golden LE vector and malformed-size/version/bit/reserved cases.
- Strict negative controls for every boundary operation; selective fallback.
- Hard vs preferred usage, distinct producer/consumer needs and resource rejection.
- Existing session-contract tests and unchanged version/wire-size guards.
- Platform builds/tests recorded separately from any actual GPU capability claim.

Tasks M5a.2 onward supply exact platform capabilities, allocation selection,
ownership/forwarding, actual control exchange and GStreamer integration. Nothing
in this task changes the supported runtime path matrix by itself.

## Actual resource facts (M5a.2)

`flImageMemoryInfo(channel, image, &info, sizeof(info))`, with version1, inspects
an image belonging to that live channel. Call under existing channel threading
and borrowed-image lifetime rules. Queries neither map nor wait for the GPU.
Unknown fields are not evidence of either support or incompatibility: check
`knownFields`. A strict path needing an unknown fact must probe or reject, not
assume it. No native handles or additional ownership are returned.

- Windows: actual DXGI format/D3D11 BindFlags, adapter LUID, shared NT handle
  protocol and timeline synchronization. CPU mapping stages a copy/readback;
  producer unmap also uploads. Native texture plane layout remains unknown.
- Linux: exported DMA-BUF fourcc, plane offsets/strides and modifier; local GBM
  allocation flags and DRM node identity. Received legacy descriptors omit
  allocation flags/device, so imported-image queries leave those unknown.
  DRM identity bytes0..3/4..7 are little-endian major/minor; others zero.
- Android: actual AHB native format/usage and AHB/sync-FD transport. BGRA CPU
  mapping converts/copies; NV12 normalization cost is known only while mapped.
  Device identity and GPU plane layout are unknown, not inferred from CPU strides.

Device IDs are local OS domains, not portable wire identifiers. Windows bytes0..7
are the DXGI LUID; remaining bytes zero. `directUsage` reports native allocation
facts only, not arbitrary API import compatibility. `mappedUsage` describes
current mapping access, not authorization to modify a consumer-owned frame.
`mapOperations` covers the whole map/unmap path. Sync transport flags describe
FrameLink API support, not successful hardware semaphore probes.

`interopCapabilities` currently exposes existing-ring import and borrowed native
handle access only. Locally adopted pools also report EXTERNAL_POOL; see [external pools](EXTERNAL_POOLS.md).
The generic RELAY capability remains unadvertised: safe forwarding requires the
explicit negotiated bindings/completion contract in GST_NATIVE_MEMORY.md and
samples/NATIVE_RELAY.md, not a resource-query bit. Handle duplication alone
cannot establish safe reuse. No claim of physical GPU residency is made.

[Device and mapping validation](m5a/adapters-20261101/README.md).

## M5a.6 — agreement across participants

`flMemoryRequirementsCombine` computes an allocation demand from up to sixteen
participants: hard and preferred usage are unions; resource and operation
permissions are intersections. A conflicting resource set rejects without
modifying the output. This is not a union of endpoint capabilities.

`flMemoryEvaluateBoundary` evaluates one exact candidate against every endpoint.
Each endpoint supplies its own demonstrated usage capabilities, while the union
of all declared boundary payload operations must be allowed by **every**
participant. A producer permitting upload cannot override a strict consumer;
a consumer permitting readback cannot override a strict producer. Missing
preferences are diagnostic only; missing hard usage always rejects. The caller
must first establish matching allocation identity, layout, device and generation.
Resource-type mismatch is invalid, not an invitation to choose a silent fallback.

If a strict candidate fails, the caller can propose another candidate, but must
not edit participants' hard demands or broaden their operation permissions.
A copied/converted candidate succeeds only with explicit consent from all
participants. Conflicting consumers reject the shared candidate; separate pools
or transformations require a separate, explicitly agreed path. Unknown driver
facts must be probed or rejected, never inferred from requested allocation flags.

Invalid/version errors leave all decisions unchanged; valid incompatible paths
return per-endpoint diagnostics. These bounded helpers do not perform I/O, map
pixels, wait for fences or select a physical memory residency. Runtime protocol,
GStreamer enforcement, legacy-peer rejection and generation-bound agreement are
M5a.7 integration work, not established by this policy validation.
