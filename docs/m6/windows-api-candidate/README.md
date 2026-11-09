# Windows API integration baseline candidate — NOT FROZEN

**Current additive CPU implementation:** `d8f97c244696c42bb4ad4124674d8b462df4a9ed`.
Focused tests pass7/7 Windows,8/8 Linux,8/8 Android plusdualABIbuilds; all59 old
DLL exports preserved plus16 CPU symbols. A clean exact-revision full Windows/API
validation is currently running; do not infer its outcome from these staged tests.
The CPU factories require separately authenticated delegated pipes/processes;
coordinator/GStreamer integration and all unresolved M6 acceptance gates remain.

**Previously verified candidate** exact revision: `609cb74031a5de2cee782619007a6e87ab7fbccd`.
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
| CPU descriptor | v1 FCS1,128-byte wire; Windows allocationBytes explicitly page-padded | Unchanged portable layout; actual Windows channel NOT AVAILABLE |
| CPU channel | Opaque channel/lease API currently Linux/Android, FD factories behind !WIN32 | Future Windows factory must be additive; never reinterpret int FD as HANDLE |
| GStreamer coordinator | `framelink.session.v1`, typed strong reference, authenticated stream binding, finite cancellation | Existing native stream tests pass; CPU Windows binding pending |
| GStreamer bins | `shared-cpu=false`; native output opt-in on avsrc; avsink lacks native producer selection | Windows CPU property presence does not imply platform support |

## Observed defaults, not a new product decision

Runtime `gst-inspect-1.0` was checked against the isolated build's actual DLL.
- Thin `framelinksink`: `buffer-wait-ms=5`, `error-on-ring-full=false`,
  `native-memory=false`, `shared-cpu=false`. Full-ring timeout therefore counts
  a dropped frame and continues unless caller explicitly enables errors.
- Strict native reservation rejects foreign buffers; no implicit copy fallback.
- `framelinksrc`: `max-queue-age-ms=0`, `pool-size=4`, native/shared CPU opt-ins off.
- `framelinkavsrc`: `latency-ms=0`, `video-pool=8`, `video-queue-frames=2`,
  `audio-queue-ms=80`; these capacities are NOT a fill-to-depth scheduling delay.
- `framelinkavsink`: `video-pool=0` (consumer-selected), `video-queue-frames=2`.
- CPU strict channel returns BUSY for held ownership, never overwrites; its
  explicit copy path remains separate from native GPU types.

The old ledger phrase “current fail-fast behavior” was overbroad: thin native
copy-in default is drop-and-count. Preserve existing behavior; strict test
profiles explicitly enable their own failure policy. D2 uniform realtime/lossless
policy remains unresolved and Jon-owned. D3 convenience defaults remain unresolved;
no automatic native-to-CPU fallback was added. No new default was silently chosen.
An app requiring no unexpected loss must select explicit strict policy and observe
loss counters; current avsink property coverage needs further stabilization.

## Migration and pending integration

D1 already changed avsrc's old150ms default to0; buffered final playback must now
set `latency-ms=150` explicitly. Final presentation synchronization remains the
application's responsibility; don't indiscriminately disable final sinks.

Planned Linux/Android integration can preserve opaque CPU channel/lease types,
FCS1 descriptor,160-byte session, memory policy, metadata and EOS versions. A future
Windows factory can accept a separately authenticated connected pipe/handle
without changing POSIX factories. Any new wire transport envelope must be tagged
and explicitly selected; do not append handle integers to legacy POSIX packets.
Windows HANDLE values are process-local; bind the process identity, transfer exact
rights, acknowledge adoption, reject stale epochs and retain held old leases.

Remaining gates before a Windows-ready API baseline: bounded authenticated CPU
control/handle transfer, CPU lease/channel/coordinator/bin integration, D3D11 native
producer coordinated-session integration, strict overload property/profile review,
resource-growth/timing/fault fixes and exact-head compatibility tests. Real decoder,
two-hop PCM and all-platform M6 acceptance remain separate. This candidate is a
reviewable inventory, **not a freeze, supported new CPU API, or M6 closure**.

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
This does not expose a Windows CPU factory or replace the exact609cb74 candidate
above. Authentication/delegation and public lease integration remain required.

## Additive Windows CPU API implementation

[Windows CPU factories and portable leases](../cpu-path/windows-cpu-channel/README.md)
now pass7/7 focused Windows tests and8/8 each Linux/Android shared regressions.
`framelink_shared_cpu_win.h` accepts authenticated pipe/process handles separately;
POSIX factories remain unchanged. All59 legacy DLL exports are retained plus16
new CPU symbols. This supersedes the “Windows channel unavailable” implementation
status above, **not** the pinned609cb74 candidate artifact. A fresh exact-revision
Windows/API build is next; new coordinator/GStreamer CPU binding, readiness timing,
native producer integration and product-policy decisions remain open. No freeze.
