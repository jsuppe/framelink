# Linux native relay parity (M6.9d)

**Decision (Jon, 2026-11-19):** full parity with the Windows local native relay, Linux first, then Android. Linux and Android get the FAR4 relay entry points and a FAR5 same-allocation equivalent: consumer-pool snapshot, authenticated pool transfer, `set-local-native-pool-candidate`, samples, the Windows relay tests' counterparts, and two-hop qualification with PCM. The Windows contract is [WINDOWS_LOCAL_NATIVE_RELAY.md](../WINDOWS_LOCAL_NATIVE_RELAY.md).

Two further decisions (Jon, 2026-11-19):
- **Typed API per platform.** The Windows FAR5 sender `fl_gst_session_open_local_native_relay_pool` takes D3D11 shared handles and an adapter LUID and stays Windows-only. Linux gets `fl_gst_session_open_local_native_relay_pool_dmabuf`, taking `flDmabuf` images; Android later gets an AHardwareBuffer variant. Device identity is checked inside the authenticated transfer, so there is no LUID argument. This mirrors the core library (`flCreateChannelFromD3D11Handles` / `flCreateChannelFromDmabufs`). `fl_gst_session_open_local_native_pool_receiver` has no platform types and keeps its name and signature.
- **Shared test sources.** The Windows relay tests move into platform-neutral sources with a small native-producer adapter (D3D11, DMA-BUF, later AHardwareBuffer), as `test/fl_gst_native_av_session.cpp` already does. Every platform runs the same cases under the same names apart from the platform suffix. Each step that touches Windows test code is revalidated on Windows (full suite and the frozen-suite check).

## Starting point

| Layer | Windows | Linux before this work |
|---|---|---|
| Same-allocation relay, `samples/native_relay.h` | yes | yes: `native_relay_linux`, `native_relay_process_linux` (identity by DMA-BUF inode) |
| Import of an external pool | `flCreateChannelFromD3D11Handles` | `flCreateChannelFromDmabufs` |
| Consumer-pool snapshot | `flSnapshotD3D11ConsumerPool` | none |
| FAR4 `open_local_native_relay_input`, `open_local_native_relay` | yes | none; the code is mostly platform-neutral but compiled for Windows only |
| FAR5 sender and `open_local_native_pool_receiver` | D3D11 handles over a kernel-pinned, separately authenticated named pipe | none |
| `set-local-native-pool-candidate` | yes | registered, always refuses |
| Samples: pool export guard, GStreamer relay adapter, setup transaction | D3D11 | none |
| Session-level relay tests | about 36 `gst_*relay*_win` | none |

## Phases

Each phase is one pull request.

1. **Core consumer-pool snapshot** (this record's first change). `flSnapshotDmabufConsumerPool`, `flQueryDmabufPoolSnapshot`, `flDmabufPoolSnapshotImage` and `flReleaseDmabufPoolSnapshot` in `framelink_dmabuf.h`, with the Windows snapshot's rules: owned duplicates of the exact original allocations; while any snapshot lives, a newly attaching producer gets `FL_BUSY` and geometry requests are answered with the current ring; snapshots outlive the source channel. Test: `dmabuf_pool_snapshot`.
2. **FAR4 relay entry points** on Linux: `open_local_native_relay_input` and `open_local_native_relay`, with setup, cancellation and source-control tests.
3. **FAR5 on Linux:** DMA-BUF fds passed with `SCM_RIGHTS` over a Unix socket, the peer pinned with `SO_PEERCRED`, the transfer authenticated under its own domain; the receiver imports with `flCreateChannelFromDmabufs` and checks geometry, pool size, distinct allocations and the render node device before acknowledging. Adds `open_local_native_relay_pool_dmabuf`, enables `open_local_native_pool_receiver` and `set-local-native-pool-candidate`, and the pool-export and relay-adapter samples.
4. **Shared relay tests:** bins, supervisor, events, cancellation, death and recovery, and the three-process A/V graph with PCM.
5. **9d Linux qualification:** two hops with PCM, allocation identity by inode and zero payload copies in the trace, with an evidence record.

Android follows with the same phases on AHardwareBuffer.

## Phase 1 result (2026-11-19)

`dmabuf_pool_snapshot` checks: producer and null arguments rejected; the snapshot names the exact allocations (distinct inodes, including the frame just read, whose pixels are read back through the snapshot's own fd); the same allocations import into a second ring; while pinned, a geometry request leaves geometry and generation unchanged, media continues, and a new producer gets `FL_BUSY` for each live snapshot; after release, attach and reallocation work again; a snapshot stays valid after both ends close.

Negative controls: removing the pin from producer attach fails the `FL_BUSY` check; removing it from the geometry path fails the unchanged-geometry check.

The Linux core library exports 83 functions (79 before, plus the four snapshot functions).

## Phase 2 result (2026-11-19)

`fl_gst_session_open_local_native_relay_input` and `fl_gst_session_open_local_native_relay` are built on desktop Linux (`FL_GST_LOCAL_NATIVE_RELAY`: Windows and desktop Linux). The FAR5 pair stays Windows-only until phase 3. The Linux session library exports 43 functions (41 before).

The shared test binary `fl_gst_native_relay` (`test/fl_gst_native_relay.cpp`, platform adapter `test/fl_gst_native_relay_platform.h`) starts with two modes moved from the Windows-only `fl_gst_native_producer_win`:
- `--relay-local-session` (`gst_relay_local_session_{win,linux}`): FAR4 relay setup pins the upstream's media, clock, epoch, generation and track IDs with a fresh hop identity; mapping, media, cancellation and alias negatives.
- `--relay-local-source-control` (`gst_relay_local_source_control_{win,linux}`): `relay_input` against a separate source process; source-relative mapping, 8 exact frames and metadata, real read leases.

The adapter holds the platform parts: the native candidate on `FL_GST_LOCAL_NATIVE_RESOURCE`, writing a producer frame (D3D11 upload or mapped DMA-BUF), one-way control gates (inherited Windows events or pipes) and child processes (job object, or fork/exec with `PR_SET_PDEATHSIG`). The Windows three-process FAR5 graph still uses its own source helper until phase 3 moves it.

Negative control: installing the ordinary playback mapping in `relay_input` instead of the source-relative one fails the source-control mapping check.
