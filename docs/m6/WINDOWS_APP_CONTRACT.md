# Windows application integration contract — not frozen

This is an app-facing reading of the implemented contracts integrated at
`a50841a`, with runtime DLL source contracts unchanged from `461584b`, not a
new default, an ABI freeze or final Windows qualification. The
[API candidate](windows-api-candidate/README.md) records178/178 integrated tests,
128 retained/additive exports and3/3 unchanged baseline clients;
[exact evidence](windows-integrated-a50841a/README.md) separates those checks
from timing, resource and duration acceptance. Follow the
[local native relay recipe](WINDOWS_LOCAL_NATIVE_RELAY.md) for FAR5 setup;
its setup, pool snapshot, separate-process A/V and automatic FAR5 receiver
contracts are implemented. Functional verification is not timing/resource acceptance.

## Supported scope and lifecycle qualification

The [approved Windows readiness checklist](M6_WINDOWS_READINESS.md) is the
candidate support boundary; this API document describes capabilities, not a promise
that every reachable combination is qualified. In particular, native BGRA1080 and
decoder4K are excluded from initial advertised support. No unrestricted repeated
native reconnect is qualified: persistent-role100-cycle slopes remain above the
original16KiB/cycle limit. Fresh bins/session per generation are an ownership rule,
not proof that process/driver resource usage returns to baseline. Ordered teardown,
real reader completion and snapshots remain mandatory even for finite sessions.
No hidden retained device, timed cleanup sleep or process termination replaces the
published close/drain/lease obligations. An opt-in device-owner API is only a proposal.

### Measured shared-CPU lifecycle

For the named Windows host/driver and BGRA/NV12 at1080p60/4K30, both endpoint
roles pass100x5s sessions with the other endpoint recreated in a fresh process.
Create fresh bins and session generation, release genuine mappings/leases and
finish teardown before the next setup. Original first5warmups and private-commit,
handle/thread slope gates apply; all800measured cycles have exact media counts.
This is measured warm lifecycle at that cadence, not arbitrary/infinite reconnect,
permission to discard held mappings or a waiver of cold one-handle observations.
See [resource evidence](cpu-path/windows-cpu-lifecycle-summary/README.md).
It does not establish the same contract for native D3D or renderer GPU resources.

### Renderer integration boundary

Approved renderer9f74b9d with repaira6a1906 accepts explicit `--av-latency-ms 0`
through2000; its default remains100ms. This differs deliberately from the A/V source
bin's default0ms. The tested NV121080p60 renderer profile has ring12/pending6 and
explicit CPU conversion. Ring12 consists of six future positions, current1,
prior-GPU2, retirement1, mailbox1 and writer1; the source comments identify this as
an operating envelope, not guaranteed lossless catch-up under arbitrary stalls.
Capacity does not imply a fill-before-play delay. Complete5s controls establish
source/acquire/adopt/render/GPU/present-submission identity and PCM accounting at
0ms and100ms; they do not establish two-slot renderer operation,30-minute renderer
resource behavior, decoder zero-copy, physical scanout or acoustic alignment.

## Select a profile explicitly

- **Native GPU:** producer `native-memory=true`; receiver
  `native-memory=true`. The local native candidate uses D3D11 producer usage
  `GPU_SAMPLE` or `GPU_SAMPLE|GPU_RENDER`, operations `0`, and receiver
  `GPU_SAMPLE`, operations `0`. `GPU_RENDER` alone is not accepted. Rendering or
  decoder conversion must target the negotiated allocations; arbitrary decoder
  textures are not made compatible by sharing an adapter.
- **Native transport with CPU readback:** producer `native-memory=true`,
  receiver `native-memory=false`, native candidate receiver `CPU_READ` with
  operations `COPY|READBACK`. This is explicitly authorized readback, not the
  zero-operation GPU profile and not shared-CPU transport.
- **Shared CPU:** `shared-cpu=true`, `native-memory=false`, using the explicit
  CPU setup/context. It has separate opaque channel/lease types and declared
  copy operations; it is not an automatic fallback after native setup fails.

Both endpoint requirements must authorize the union of actual operations.
Allowing an operation is different from performing it. Conversely, declaring
zero operations at a boundary does not establish decoder-internal zero copy,
GPU compatibility, or physical presentation latency. Do not retry a rejected
native profile silently as a CPU profile. Automatic native candidates are
Windows-only; a property/action being present is not proof of platform support.

## Configure, bind, then start

1. Create fresh bins in NULL. Set all properties, including the nonempty
   current-user discovery directory, profile, caps, pool/queue sizes and timeout
   controls, before binding. A discovery path is not a secret or an arbitrary URL.
2. For local native setup, copy the same producer-ordered version-1 candidate
   (`sessionGeneration=0`) into both bins with `set-local-native-candidate`.
   Check its Boolean result. TRUE only accepts and freezes the template; it
   does not mean peer authentication, allocation or readiness succeeded.
   For a FAR5 same-allocation relay output, the final receiver instead uses
   `set-local-native-pool-candidate`: A/V only, explicit pool >=2, and the same
   NULL-only copied/frozen template contract. This is explicit selection, not
   fallback from FAR4. Check action availability for older plugins.
3. Observe the bus for asynchronous setup errors. For explicit broker adoption,
   authenticate offers and verify the immutable allocation binding first;
   `apply-memory-policy` requires an adopted channel and a nonzero generation,
   with endpoint 0 referring to the local element. A policy argument does not
   authenticate itself. Do not interchange bound policies and templates.
4. Keep the documented session/channel references alive and honor readiness and
   drain APIs. Recreate bins and session for a new setup attempt after failure,
   cancellation or reconnect; do not mutate an accepted candidate in place.

Pool/queue capacities are not target playback delays. The local setup contract
accepts queue depth 1 with pool >=2; larger depths require `queue + 2 <= pool`.
A global pool=2 change while retaining queue=2 is invalid. Set both values
explicitly for a small-pool recipe, and require the actual producer/consumer
ownership and completion path to support that configuration.

## Defaults and overload accounting

The existing defaults remain unchanged: thin video sink and A/V sink
`buffer-wait-ms=5`, `error-on-ring-full=false`; A/V sink `video-pool=0`
(consumer-selected), `video-queue-frames=2`; A/V source `video-pool=8`,
`video-queue-frames=2`, `audio-queue-ms=80`, `latency-ms=0`.

On applicable video copy-in paths, ring-full timeout drops and counts the
incoming frame unless `error-on-ring-full=true`. An application choosing strict
copy-in behavior must explicitly set that flag before binding. The wait property
is an allocation wait (0 is nonblocking), not playout latency. These controls
neither allow overwriting held native readers nor make the shared-CPU channel or
PCM lossy. They also do not implement a general latest-frame replacement policy.

For the existing EOS accounting surface, watch `GST_MESSAGE_ELEMENT` on the bus
and match structure name `framelink-sink-stats`. Its uint64 fields are `received`,
`submitted`, `dropped`, and `ring-full-drops`. For A/V bins the message originates
from the internal video sink; attribute it to the owning bin/session, not only
messages whose source equals the outer bin. Generic GstBaseSink stats do not
replace these transport counters. These are aggregate counters emitted on the
successful video EOS path, not continuous per-frame/per-identity loss telemetry.
There is no corresponding top-level bin stats property or guaranteed final
snapshot after cancellation/error. Do not infer zero loss from a missing message.
Applications requiring accounting on abnormal termination need additional
instrumentation; this document does not promise a new public stats API.

The earlier A/V source 150ms latency default is now 0. For intentional buffered
final playback, set `latency-ms=150` explicitly. Final sink synchronization is
still application-owned; do not indiscriminately disable it to lower transport
latency.

## Ownership and failure rules

- Native texture/device/shared-handle accessors are borrowed as documented.
  An extra COM reference or duplicated OS handle keeps a resource alive but does
  not retain its FrameLink reader lease. Retain the exact native GstMemory until
  actual asynchronous GPU reads complete. Android AHB references have the same
  distinction. Cancellation, device failure and peer death are not completion.
- Finish writes before abandoning an unpublished native reservation. After
  publication do not write through retained aliases. A second independent pool
  does not authorize forwarding the first pool's buffers; use authenticated
  same-allocation binding and the read-lease relay contract.
- CPU factories consume the passed connected FD/pipe on **every** path. Windows
  peer-process and optional cancellation-event inputs are borrowed/duplicated;
  factory output pointers remain unchanged on failure. This output rule is not
  universal: local native setup clears its outputs on failure.
- `fl_gst_session_claim_shared_cpu` transfers ownership once per generation for
  the exact role. The caller closes the claimed channel; session cancellation or
  reconfiguration interrupts I/O without freeing claimed channels/leases.
- Externally serialize calls sharing a CPU channel or its leases. Write submit
  consumes the write lease on success and failure; every successful read retain
  requires a release. Closing disconnects but held mappings survive until their
  lease releases. Native raw-handle lifetime rules must be read separately;
  CPU mapping survival is not a blanket guarantee for raw native pointers.
- Raw CPU ordered EOS requires explicit matching flags on both peers; coordinated
  CPU setup requires its metadata/EOS contract. EOF, timeout and death never mean
  EOS, release, or permission to reuse a held slot. Keep draining real releases
  after ordered EOS. Reject stale generations and mismatched negotiated flags.

Timeout scopes differ. Windows raw CPU factories bound each I/O and local
allocation preparation, not the entire factory. Local native setup shares a
setup-I/O deadline but still includes synchronous platform allocation calls.
Do not derive an end-to-end GPU/driver cancellation guarantee from
`connect-timeout-ms`. See the function-specific headers for the exact scope.

## Same-allocation relay ownership

`flSnapshotD3D11ConsumerPool` creates an opaque snapshot that pins the input
pool/epoch while its borrowed handles establish the explicit FAR5 output.
Retain that snapshot for the entire relay graph, not just setup. Release it
using its API; it is not a reader lease and does not grant
permission to reuse a frame still held by a reader. Pool replacement and new
producer attachment remain excluded while the snapshot pins that pool.

The checked sample `setupLocalNativeRelay` composes source-relative FAR4 input,
snapshot and FAR5 output as one unpublished transaction. It clears empty output
resources on failure and cancels the two fresh caller-owned sessions, but does
not publish readiness, install media adapters or own a running graph's teardown.
Its remaining setup-I/O deadline does not bound synchronous driver calls. Install
adapters/supervision before downstream-then-upstream readiness.

`SessionPcmRelay::endPublication` permits authenticated terminal publication
with outstanding native readers; `finish` retains its strict drain requirement.
Neither EOS, cancellation nor terminal publication completes native reads.
Accept real control drain before closing the session, retain native GstMemory
through actual GPU completion, and collect genuine releases before reclaiming
native resources. The application may retain its own native GstMemory reference
across receiver bin NULL; an unreferenced buffer is not retained merely by stopping the bin.

## Compatibility and decisions still open

Two Windows behavioral changes require caller attention: an explicitly selected
unavailable D3D adapter now returns `FL_ADAPTER_MISMATCH` instead of silently
choosing the default adapter; legacy producer-open failure clears the output
after valid name/output argument checks (a null name returns before clearing it).
Always check the result and do not pass storage holding a live channel.
The additive bounded producer-open path requires negotiated pull-handle support
and returns `FL_VERSION_MISMATCH` for an older peer; the legacy protocol remains
available. FAR1–FAR4 formats and existing bin defaults are unchanged.

Before calling a new API from a client supporting older DLLs/plugins, detect its
symbol/action/property availability and reject unavailable capabilities
explicitly. Exported-name retention alone does not establish struct layout,
calling conventions, ordinal imports or old-client behavioral compatibility.
No stable ordinal-import promise is established by the current evidence; clients
should use named imports/runtime name lookup, not assume ordinal stability.

Final qualification must pin the integrated source, public headers and loaded
DLL/dependency hashes; compare all public layouts/enums and export names/ordinals;
compile C11/C++ clients; run old-built clients against new DLLs; and exercise
ownership, failed outputs, missing capabilities, immutable binding, protocol
negatives, overload, restart and held-reader behavior. Preserve portable FCS1
128-byte and session 160-byte encodings and typed platform factories; Windows
HANDLE integers are not portable FD descriptors. Final integrated, timing and
resource evidence remains required beyond compilation and export inspection.

**D2 remains Jon-owned:** the default for new convenience callers (strict bounded
failure versus explicitly specified realtime/latest behavior). **D3 remains
Jon-owned:** whether any new convenience entrypoint may choose a compatibility
copy profile. Current recommendation is explicit strict/native and separate
opt-in CPU/readback profiles, without altering existing defaults. These decisions
do not block correcting documentation or completing already-authorized code.
M6 remains open; M7 packaging/release remains held.

Source contracts: [memory policy](../../gst/framelink_memory_policy.h),
[session transport](../../gst/framelink_session_transport.h),
[CPU leases](../../include/framelink_shared_cpu_channel.h),
[Windows CPU factories](../../include/framelink_shared_cpu_win.h),
[native memory](../GST_NATIVE_MEMORY.md).
