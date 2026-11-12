# Windows local native relay integration contract

Implementation: `c4f164a`, `c0aa02e`, `0a430f9`. These additive APIs are not a frozen app baseline. The last integrated Windows candidate remains `7b8a274`; focused new-path evidence is linked below.

## Choose the explicit setup profile

| API | Peer API | Pool contract |
| --- | --- | --- |
| `open_local_native` / `open_local_native_epoch` | `open_local_native` | FAR4, newly allocated native pool |
| `open_local_native_relay` | `open_local_native` | FAR4, preserved upstream timeline but **independent** downstream pool |
| `open_local_native_relay_pool` | `open_local_native_pool_receiver` | FAR5, authenticated import of caller-pinned original allocations |

Names above omit the `fl_gst_session_` prefix. FAR5 rejects FAR4, CPU FAR3 and legacy endpoints; it never falls back to another profile. The A/V bin `set-local-native-candidate` action selects FAR4, **not** FAR5. Setting `native-memory` alone selects neither authenticated local profile.

A version-1 candidate has generation zero and producer-first endpoints on both peers. The producer path is D3D11 `GPU_SAMPLE` or `GPU_SAMPLE|GPU_RENDER`, operations zero; `GPU_RENDER` alone is not accepted. Receiver choices are `GPU_SAMPLE` with operations zero, or explicit `CPU_READ` with `COPY|READBACK`. Both requirements must authorize the selected boundary. Neither policy acceptance nor handle authentication proves arbitrary decoder compatibility or GPU completion.

## Owner-controlled composition order

1. Keep a live, identity-mapped A/V upstream session. Serialize configuration and hold session references throughout setup. Match its exact media, clock, epoch, generation and track IDs; downstream identity/credentials are new. Do not substitute a newly chosen epoch.
2. Before source publication, pin **all** unpublished allocations of the trusted source pool. `samples/native_d3d11_relay_pool.h` provides the single-attempt `NativeD3D11RelayPoolExport` guard for an active FrameLink producer GstBufferPool. It rejects busy, partial, foreign, stale or upload-needed buffers. Do not harvest an incomplete pool from in-flight frames. The raw C API borrows the LUID and handle array and relies on the caller to establish this origin/ownership.
3. On setup workers, pair FAR5 sender and receiver at the same protected local path with identical candidates. Keep the export reservations until setup returns. The authenticated receiver pulls duplicates from the pinned process; the wire values are not local handles. Import validates actual geometry, capacity and adapter LUID before acknowledgment. Partial imports are closed on failure; the source handles remain borrowed and untouched.
4. On success, each caller owns its returned channel. Keep the source pool alive for the complete relay lifetime. Configure every element property in NULL before binding. Apply the bound nonzero-generation policy in local-endpoint order: sender unchanged, receiver with requirements and paths swapped. Connect a read-lease relay matching actual allocation identity, **not slot index**, before source writes. Independent writers to the two rings are invalid.
5. Bind/register the actual video and PCM tracks, preserve the agreed timestamp mapping and forward ordered events with their frame barriers. Establish `local_ready` only after track setup. Install ongoing supervision explicitly, e.g. `samples/session_relay_supervisor.h`; setup itself does not install a media adapter or supervisor. The executable composition reference is `test/fl_gst_native_bin_relay_win.inc` with the FAR5 setup in `test/fl_gst_native_producer_av_win.inc`.
6. Retain the original native GstMemory/read lease through **actual final GPU completion**. A texture COM reference or duplicate handle alone is not a FrameLink read lease. Cancellation, timeout, process death and device failure never authorize reuse of a live reader's allocation.
7. Finish successfully only after real aggregate video/PCM/event drain and final-reader release. Then stop/join the supervisor before normal endpoint teardown and perform the local terminal handshake. On failure cancel rather than manufacture END/EOS or successful drain. New graph/session/bin objects are required for reconnect; do not rebind a frozen candidate or reuse an old generation's leases.

## Bounds, defaults and scope

- FAR5 setup watches upstream cancellation/configuration changes and interrupts pending discovery/transport I/O. Its whole-call I/O budget is not a hard bound on synchronous native driver/create/open calls. Ongoing post-setup cancellation needs the installed supervisor.
- Queue 1 permits pool 2; queue greater than 1 requires pool at least queue+2. This is capacity, not a mandatory fill delay. Legacy defaults are unchanged. No new universal latest/lossless policy is implied; product D2/D3 decisions remain open.
- `error-on-ring-full` is not a universal delivery policy. Explicit CPU/readback choices are not automatic fallback. Never silently copy after native authentication or import failure.
- There is no public pre-media enumeration/pinning API for a separate native receiver to export an entire source pool. Current composition requires caller-owned source exports. The existing real-process test covers source/relay in one process and final reader in another; it is **not** qualification of a general three-process A/V app.
- Native local setup is Windows-only. No Linux/Android automatic-native parity, full ABI compatibility, timing/endurance acceptance or M6 completion is claimed.

## Focused evidence

- [Authenticated import and pinning](windows-relay-auth-pool/README.md)
- [FAR5 setup, live A/V composition, process boundary and malformed imports](windows-local-relay-pool/README.md)
- [Source-setup cancellation and bidirectional live supervision](windows-local-relay-cancel/README.md)

Read-only independent review found no actionable defects in the authentication, cleanup, identity and cancellation scope. It ran no runtime tests and does not remove the bounds or qualification gaps above.

## Prepared next focused controls (not yet run)

Commit `94e8952` adds an independent authenticated FAR4 source child and upstream-only control (`gst_relay_local_source_control_win`), plus FAR5 serialized GAP/DISCONT/trailing-GAP integration (`gst_relay_local_events_win`). These are scaffolding and focused checks, not a completed three-process snapshot integration. Their compile/runtime results remain pending the backend owner's Windows-lane handoff.

`tools/validate_local_native_relay.ps1` is an explicit post-handoff runner. It requires the coordinator's runtime release switch, a clean exact revision and a new evidence directory; refuses an observed existing build/test executor; checks that all four named tests are selected; and records source hashes, build/test logs and terminal status. It does not run automatically, qualify ABI/performance or replace the coordinator's host ownership. PowerShell execution/parse validation is pending on Windows.
