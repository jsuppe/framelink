# Windows API integration baseline candidate — NOT FROZEN

**Current verified integrated candidate:** `32963c51fba63caf768d1dee0cd9aa4bd11a0ad8`.
Clean exact-revision Windows build/API/full suite **135/135 passed**;
[pinned evidence](../windows-integrated-32963c5/README.md) includes DLL hashes,
runtime properties and exports. All 119 prior exported names retained (119 total).
Export-name retention is not proof of behavioral or ordinal ABI compatibility.

This revision includes explicit Windows local native setup, the additive
`fl_gst_session_open_local_native_epoch` API and automatic native A/V bin setup
via `set-local-native-candidate`. [Native bin contract and checks](../windows-native-bins/README.md):
configure all properties before copying the immutable candidate; fresh bins are
required for each setup attempt, including reconnect after failure/cancellation.
This includes explicit D3D11 GPU_SAMPLE/operations=0 receiver support as well as
the CPU-readback profile. The explicit GPU_RENDER producer candidate supports the tested decoder/color/scale path; raw private decoder allocations are rejected. Neither qualifies a two-hop pipeline. No overload/fallback defaults changed.

Windows authenticated CPU discovery/delegation and `shared-cpu=true session=`
bins are implemented and tested. Native producer and receiver bins support
broker-bound channel and immutable policy adoption. Full configured session and
native policy candidates can now be authenticated through either explicit
connected streams, the Windows local helper or explicitly candidate-bound bins. Exact pixels/PCM and37ms mapping
pass; real decoder and two-hop gates remain open. The earlier
Windows startup failure is not waived by later integrated passes. API not frozen;
M6 not complete.

**Historical candidate** exact revision: `609cb74031a5de2cee782619007a6e87ab7fbccd`.
Clean isolated Windows source archive SHA256:
`ab8efb73b7b8282a9d64d8edf226cb379d18b3c89d39ae4e606c956e083d6da6`.
Remote source/build: `<win-home>/dev/framelink-m6-windows-609cb74`.
Full Release build completed, then **31/31** selected API/ownership/fault/native
producer/consumer tests passed. This supersedes staged-tree ambiguity for these
specific tests, not full M6 qualification. Source headers are pinned in
`header-manifest.json`; three DLL hashes and runtime element properties are in
`api-runtime-inspection.log`.

## Surface inventory / compatibility

| Surface | Observed contract | Status |
| --- | --- | --- |
| Native C `framelink.h` | FL_VERSION6; borrowed image/frame handles, explicit acquire/release; metadata v1 opt-in | Existing symbols/layout unchanged by this recovery |
| D3D11 platform header | External immutable texture ring, same adapter, explicit GPU completion; borrowed texture/device references | Existing tests pass; coordinated real-producer M6.9 not closed |
| Memory policy | v1 requirements; no operations allowed after init; both endpoints permit actual COPY/READBACK/UPLOAD/CONVERT union | Policy is not driver capability or zero-copy proof |
| Session | v1 A/V, explicit v2 optional tracks,160-byte wire; newer generation and shared A/V clock mapping | C/C++/peer tests pass |
| CPU descriptor | v1 FCS1,128-byte wire; Windows allocationBytes explicitly page-padded | Unchanged portable layout; Windows pipe channel available explicitly |
| CPU channel | Portable opaque channel/lease API; POSIX FD and additive Windows pipe factories | Separate typed platform factories; never reinterpret int FD as HANDLE |
| GStreamer coordinator | `framelink.session.v1`, typed strong reference, authenticated stream binding, finite cancellation | Windows explicit binding or protected local authenticated composition available |
| GStreamer bins | `shared-cpu=false`; native output opt-in on avsrc; broker-bound native producer opt-in on avsink | Windows broker-context and explicit CPU session= discovery tested; current-user-only directory required |

## Observed defaults, not a new product decision

Runtime `gst-inspect-1.0` was checked against the isolated build's actual DLL.
- Thin `framelinksink`: `buffer-wait-ms=5`, `error-on-ring-full=false`,
  `native-memory=false`, `shared-cpu=false`. Full-ring timeout therefore counts
  a dropped frame and continues unless caller explicitly enables errors.
- Strict native reservation rejects foreign buffers; no implicit copy fallback.
- `framelinksrc`: `max-queue-age-ms=0`, `pool-size=4`, native/shared CPU opt-ins off.
- `framelinkavsrc`: `latency-ms=0`, `video-pool=8`, `video-queue-frames=2`,
  `audio-queue-ms=80`; these capacities are NOT a fill-to-depth scheduling delay.
- `framelinkavsink`: `video-pool=0` (consumer-selected), `video-queue-frames=2`,
  `buffer-wait-ms=5`, `error-on-ring-full=false`.
- CPU strict channel returns BUSY for held ownership, never overwrites; its
  explicit copy path remains separate from native GPU types.

The old ledger phrase “current fail-fast behavior” was overbroad: thin native
copy-in default is drop-and-count. Preserve existing behavior; strict test
profiles explicitly enable their own failure policy. D2 uniform realtime/lossless
policy remains unresolved and Jon-owned. D3 convenience defaults remain unresolved;
no automatic native-to-CPU fallback was added. No new default was silently chosen.
An app requiring no unexpected loss must select explicit strict policy and observe
loss counters; avsink exposes explicit video ring-full controls (4a49a92 onward).

## Migration and pending integration

D1 already changed avsrc's old150ms default to0; buffered final playback must now
set `latency-ms=150` explicitly. Final presentation synchronization remains the
application's responsibility; don't indiscriminately disable final sinks.

Planned Linux/Android integration can preserve opaque CPU channel/lease types,
FCS1 descriptor,160-byte session, memory policy, metadata and EOS versions. The additive Windows factories accept a separately authenticated connected pipe/handle
without changing POSIX factories. Any new wire transport envelope must be tagged
and explicitly selected; do not append handle integers to legacy POSIX packets.
Windows HANDLE values are process-local; bind the process identity, transfer exact
rights, acknowledge adoption, reject stale epochs and retain held old leases.

Remaining gates before a Windows-ready API baseline: real-producer/relay integration,
strict overload profile review,
resource-growth/timing/fault fixes and exact-head compatibility tests. Real decoder,
two-hop PCM and all-platform M6 acceptance remain separate. This candidate is a
reviewable inventory, **not a freeze, full Windows app-integration acceptance, or M6 closure**.

## Reproduction / environment

`run.py` archives the pinned source, checks the transferred archive hash, configures
Visual Studio17 2022, builds all Release targets, and runs the recorded31-test regex.
It deliberately refuses an existing destination; use a fresh path for a repeat.
Runtime inspection requires BOTH GST_PLUGIN_PATH and PATH to include build/Release
plus the GStreamer bin directory, and an isolated GST_REGISTRY. A probe setting only
GST_PLUGIN_PATH failed to resolve sibling DLLs; negative retained, corrected probe
passed. This is not a clean-install qualification (M7 remains on hold).

## Subsequent Windows-only internal prerequisites

[Pool setup](../cpu-path/windows-cpu-control/README.md) and
[frame protocol](../cpu-path/windows-cpu-frames/README.md) now pass focused tests.
FCW1 is a separately tagged Windows section envelope; POSIX FCP1 and public
headers are unchanged. FCF1/FCR1, FCM1 and FCE1 retain existing portable layouts.
Those internal prerequisites alone did not expose a Windows CPU factory. The
subsequent d8f97c2 candidate below adds explicit public factories and leases;
automatic authenticated discovery/delegation and bin integration remain required.

## Additive Windows CPU API implementation

[Windows CPU factories and portable leases](../cpu-path/windows-cpu-channel/README.md)
now pass7/7 focused Windows tests and8/8 each Linux/Android shared regressions.
`framelink_shared_cpu_win.h` accepts authenticated pipe/process handles separately;
POSIX factories remain unchanged. All59 legacy DLL exports are retained plus16
new CPU symbols. This supersedes the “Windows channel unavailable” implementation
status above; the pinned609cb74 artifact is historical, superseded by tested d8f97c2. The exact d8f97c2
Windows/API build passed127/127; new coordinator/GStreamer CPU binding, readiness timing,
native producer integration and product-policy decisions remain open. No freeze.

## Subsequent coordinator implementation

[Windows broker seam](../cpu-path/windows-cpu-coordinator/README.md) now adds exact
generation/role binding and cancellation to the portable single-claim API.
Windows6/6, Linux4/4 and Android4/4 plusdualABI builds pass. This is newer than
the pinned d8f97c2 candidate; clean exact-head integration is pending. Automatic
authentication/discovery, CPU elements/bins and coordinated native producer gates
remain open. No API freeze or overload/default decision is inferred.

## Additive video overload controls (after d9585a8)

`framelinkavsink` exposes `buffer-wait-ms` (0..1000, default 5) and
`error-on-ring-full` (default false). Set these in NULL **before** adopting the
native channel/policy. Selection survives internal video child reconstruction;
state/policy-bound mutation is refused. Copy-in ring exhaustion preserves the
existing bounded drop-and-count default, or reports an error when explicitly
selected. `buffer-wait-ms` is allocation waiting, not playout latency.

These controls do not weaken native producer reservation/completion contracts,
select a hidden copy fallback, or permit PCM sample loss. Shared-CPU remains its
existing strict channel policy; `error-on-ring-full=false` does not make it lossy.
This additive surface does not settle D2's product-wide overload default or D3.
Use a runtime property check when supporting older bins. Native C structs and
wire versions remain unchanged.

Focused verification: [Windows4/4, Linux3/3, Android3/3 and dual ABI builds](../windows-bin-overload/README.md).

## Additive authenticated native-memory agreement

`fl_gst_session_authenticate_native_memory` proves exact producer-ordered policy agreement with the full configured session and a distinct credential domain. It does not select offers, probe/import native allocations, discover a peer or apply element policy. Legacy entrypoints/defaults/wires are unchanged. [Focused cross-platform checks and Windows A/V integration](../windows-native-memory-agreement/README.md) passed. API not frozen. The exact [c1a517f integrated candidate](../windows-integrated-c1a517f/README.md) passed133/133 with clean build/API exit0 and all116 prior exports retained (117 total).

The preceding clean [4a49a92 Windows candidate](../windows-integrated-4a49a92/README.md) passed133/133. Corrected dumpbin accounting includes decorated internal exports:116/116 prior names retained (earlier114 statements were incomplete counts, not lost exports). This is symbol-name compatibility, not a complete ABI proof.

## Additive receiver-bin policy action

`framelinkavsrc` now forwards `apply-memory-policy` for broker-adopted video,
including explicitly permitted readback. Endpoint zero is the receiver; callers
must reorder both requirements and paths from producer-ordered agreement.
Set all properties before binding. Success freezes bin configuration; a new
bin/session is required for reconnect. No automatic discovery or authentication
is implied by this action. Check `g_signal_lookup` when supporting older bins.
Native C/wire layouts and existing unbound defaults are unchanged.

See [receiver policy evidence and limits](../windows-receiver-policy/README.md).
This removes the need to reach into the receiver child in broker-native A/V
integration; it does not settle D2/D3 or freeze the app-integration contract.

## Additive native-session epoch selection

`fl_gst_session_open_local_native_epoch` accepts a producer-selected monotonic
system-clock epoch through public `FlGstNativeEpochFunc`. Receiver callbacks are
ignored; negative producer epochs reject setup. The original entrypoint retains
its default and all 118 prior exports remain. [C11 linkage, exact epoch, A/V and
negative checks](../windows-native-epoch/README.md) passed. This is an automatic
bin prerequisite, not completed bin wiring or a frozen integration contract.
The prior exact [34a391d baseline](../windows-integrated-34a391d/README.md) passed
133/133 tests; clean integration of this additive API is the next gate.


## Explicit automatic native A/V bin profile

Both A/V bins expose `set-local-native-candidate(pointer) -> gboolean`. Configure
all properties in NULL first, including the same nonempty `session=` path.
The producer selects `native-memory=true`. For the CPU-readback profile the receiver selects
`native-memory=false`; the later explicit GPU receiver profile below selects true. Both use
`shared-cpu=false` and include video. Pass the same producer-ordered
`GstFrameLinkMemoryPolicy` template: version 1, generation 0, D3D11 GPU_SAMPLE
producer with operations 0, CPU_READ receiver with COPY|READBACK. Both endpoint
requirements must authorize the paths. Unsupported profiles reject; no fallback.

Success copies the template and freezes configuration; it is NOT authentication
success. Worker setup authenticates the exact candidate before allocating channels,
then adopts and applies the bound policy internally (reordered for the receiver).
Only after setup/consumer readiness does media proceed. The producer pipeline
base time is the session epoch. Setup errors surface on the bus. Each configured
bin allows one setup attempt: create new bins for reconnect, including after
cancellation/failure. Broker actions cannot be mixed with the candidate action.

The action is discoverable with `g_signal_lookup`; other platforms return false
until supported, preserving their existing profiles. Existing native C structs,
wire formats and overload/fallback defaults are unchanged. This Windows profile
is not end-to-end zero-copy, arbitrary decoder compatibility or full M6 acceptance.
See [implementation and focused evidence](../windows-native-bins/README.md).
The preceding exact [c8ff226 baseline](../windows-integrated-c8ff226/README.md)
passed134/134; the new bin implementation requires its own clean integrated check.


## Explicit native receiver extension (included in the pinned candidate)

Windows local helper and `set-local-native-candidate` also accept D3D11
GPU_SAMPLE at both endpoints with operations=0. Select `native-memory=true`
on both bins before submitting the immutable candidate. The helper creates a
consumer channel with deferred release. Receiver property and policy must match;
a CPU_READ candidate still requires receiver `native-memory=false` and
COPY|READBACK. No inferred fallback, new default, public struct, symbol or wire
change. Older implementations reject the new GPU receiver profile; applications
must handle setup rejection, never silently retry a different memory contract.

The downstream native consumer must answer ALLOCATION with encoded
`framelink-memory-requirements` and the explicit
`framelink-reader-completion-contract=retain-memory-until-gpu-complete-v1`.
Retain each GstMemory until its actual GPU work is complete. Missing agreement
rejects negotiation. A policy declaration alone does not prove decoder support,
no hidden driver work, target-rate performance or bounded lifetime resources.

[Local native receiver evidence](../windows-local-native-receiver/README.md)
separates broker, helper and automatic-bin tests. Real decoder and two-hop PCM
qualification, original startup/lifetime failures, and D2/D3 remain open.
The pinned candidate artifact includes this extension and passed the clean integrated
suite. API not frozen.

Current real-decoder prerequisite: [8/8 actual private NV12 buffers reject without implicit copy](../windows-decoder-contract/README.md). A caps match does not authorize decoder array import or completion semantics.

## Explicit render-producer extension (newer than pinned candidate)

Windows local native helper and both bins additionally accept producer
`paths[0].supportedUsage = GPU_SAMPLE | GPU_RENDER`, `operations = 0`;
set producer `requiredUsage` to the usage the application actually needs.
The helper requests `FL_GPU_RENDER` on the channel. Receiver profiles stay
explicit GPU_SAMPLE/0 or CPU_READ/COPY|READBACK. Render-only and unrelated masks
are not supported by this profile. Existing sampling-only callers are unchanged.
Older helpers reject the additional profile; handle that rejection without
silently changing the contract. Wire layouts, C symbols and defaults are unchanged.

[Real-decoder A/V evidence](../windows-decoder-render/README.md) covers explicit
upstream GPU conversion/scaling into the issued ring, exact relative video timing,
synthetic PCM and both receiver profiles. Raw private decoder arrays are still
not accepted. This implementation needs its clean exact-revision integration
check before replacing the pinned candidate. Two-hop/timing/lifetime acceptance
and D2/D3 remain open; this is not API freeze or full Windows acceptance.

## Checked relay sample: serialized PCM events

The Windows checked PCM relay sample now fails closed on GAP and unsupported
serialized media events, including queued events immediately before EOS. It does
not silently discard them or synthesize silence. Existing explicit buffer DISCONT
is preserved. Apps requiring successful GAP forwarding still need a bounded,
ordered event-capable adapter; this safety repair is not that implementation.
The public C/wire/properties/defaults are unchanged. See
[negative controls and focused Windows checks](../windows-relay-audio-events/README.md).

## Checked relay sample: terminal wait cancellation

The checked PCM terminal rendezvous observes cancellation/replacement of either
session while awaiting authenticated upstream terminal state. Waits are sliced
without resetting the caller deadline; ordinary timeout is retryable. This sample
repair does not change C symbols, wire layouts, properties or defaults. See
[Windows negative control and checks](../windows-relay-terminal-cancel/README.md).
