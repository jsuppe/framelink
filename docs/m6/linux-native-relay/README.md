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
4. **Shared relay tests:** bins, supervisor, events, cancellation, death and recovery, and the three-process A/V graph with PCM. Two pull requests: 4a moves the controls and the in-process GStreamer bin relays (with the Linux pool-export and GStreamer relay samples); 4b moves the broker-composed process tests. The hardware-decoder relay moves to phase 5.
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

## Phase 3 result (2026-11-19)

FAR5 on desktop Linux.

- **Session:** `fl_gst_session_open_local_native_relay_pool_dmabuf` (sender, taking `flDmabuf` images) and `fl_gst_session_open_local_native_pool_receiver` (receiver, same signature as Windows). The Linux session library exports 45 functions.
- **Transfer** (`gst/fl_gst_native_relay_pool_linux.h`): an abstract `AF_UNIX` `SOCK_SEQPACKET` socket whose name is derived from the session secret. Each side checks `SO_PEERCRED` (same effective user) and binds both process IDs into a mutual HMAC under the domain "FrameLink authenticated Linux native relay pool v1". The offer carries the slot layouts with every plane fd as `SCM_RIGHTS`. The receiver checks capacity, layout, distinct allocations (by inode) and that its render node is the sender's, imports with `flCreateChannelFromDmabufs`, and acknowledges; the sender attaches as producer, checks the ring and confirms.
- **Device identity:** on Linux, `flChannelInfo.adapterLUID` now names the ring's DRM render node (major and minor, the same bytes `flImageMemoryInfo` reports), and producers receive it in the handshake as on Windows. The render-node choice is shared by the core and the session library (`src/fl_render_node.h`).
- **Element:** `set-local-native-pool-candidate` works on desktop Linux.
- **Sample:** `samples/local_native_relay_setup.h` (FAR4 input, consumer-pool snapshot, FAR5 output as one transaction) builds on both platforms.
- **Core fix:** on Linux, `flAcquireBuffer` applied a release that arrived just before the consumer closed but then reported `FL_DISCONNECTED` without handing out the freed slot. It now uses the Windows order: a free slot is returned first, and the disconnect is reported only when none is free. The relay depends on that final release as proof of completion. Android has the same pattern and is left for its phase.

**Tests moved into the shared binary**, under the same names on Windows and Linux (`gst_relay_<name>_{win,linux}`): `local_process`, `local_pool_failures`, `local_three_process`, `local_three_process_av`, `local_replacement`, `local_recovery`, `local_bin`, `local_bin_negatives` and `setup_negatives`. The Windows-only `fl_gst_native_local_source_process_win.inc` is gone. The test list is one CMake list registered per platform.

On Linux the pool-failure `Mismatch` case is a malformed plane layout; the Windows case is a wrong adapter LUID. A render-node mismatch needs a second GPU and was checked with a negative control instead.

**Fixture fix in `local_bin`:** the parent forwarded frame n+1 while the receiver bin might not yet have taken frame n, and a coordinated ring correctly refuses an unread replacement ("coordinated video frame replaced before it was read"). The bin reader now signals after pulling each video sample, and the parent waits for it. This applies on Windows too.

**Checks:**
- All 11 shared relay tests pass on Linux, and 10 repeated runs of the set under `-j4` passed.
- `local_bin` failed 13 of 30 serial runs without the read gate, and 15 of 40 parallel runs without the acquire-order fix; with both, 100 of 100 passed.
- Negative controls: a wrong role byte in the sender MAC check, and a different render node at the receiver, each fail FAR5 setup.

## Phase 4a (2026-11-19)

### Controls

**Moved into the shared binary:** `video_event_controls`, `supervisor_controls`, `local_bin_pause` and `local_bin_paused_cancel` (`gst_relay_<name>_{win,linux}`). The Windows test `gst_video_relay_event_controls_win` is now `gst_relay_video_event_controls_win`; the historical a50841a record keeps the old name. `fl_gst_native_local_pool_process_win.inc` and `fl_gst_video_relay_events.inc` are gone. `relay_pool_controls` moves with the bin relays, since it needs the Linux pool-export sample. A test list entry may carry its own timeout (`name|flag|seconds`).

**Element fix (all platforms):** after a live PLAYING to PAUSED to PLAYING cycle, the video source could stop delivering for good. GStreamer 1.28's base source calls `unlock()` on pause, restarts the streaming task on PLAYING, and defers `unlock_stop()` to the next `create()`. A `create()` that saw the unlock but returned `FLUSHING` only after the resume therefore paused the already-running task permanently. The video source now handles an unlock the way the audio reader already did: a live source that is not really flushing (source pad not flushing) waits for PLAYING with `gst_base_src_wait_playing()` and continues, with a fresh deadline. A real flush or stop still returns `FLUSHING`. Unlocks are counted, so a pause that arrives during the wait is never lost. The shared-CPU path keeps its behavior.

**Fixture fix in the pause tests:** the same per-frame read gate as `local_bin` (the bin reader signals after pulling each video sample).

**Checks:**
- Linux: full suite 159 of 159 (1 known skip). The relay set (15 tests) passed 40 repeated rounds under `-j8`; before the element fix, the same run failed `local_bin_pause` within 25 rounds twice. The reader's GStreamer log showed `create returned -2 (flushing)` after `live running 1` and the video task paused with reason `flushing`.
- Without the read gate the pause tests failed 4 and 6 of 10 runs; with it, 30 of 30 each.
- Windows: build and exports unchanged (88, 47, 2); the 22 `gst_relay_*_win` tests pass, and 25 repeated rounds of the two pause tests under `-j4`; full suite 181 of 181; the unchanged 060c54e4 suite against the new DLLs 180 of 180.
- Android: 95 of 97 (the known skip and the known `gst_av_bench_smoke` host-path failure).

### In-process GStreamer bin relays

**Moved into the shared binary** (`test/fl_gst_native_bin_relay.inc`, included by `fl_gst_native_relay.cpp`): `bin`, `lease`, `bin_cancel`, `bin_events`, `bin_supervised_downstream`, `bin_supervised_upstream`, `pool_lifetime`, `authenticated_pool`, `local_pool`, `local_cancel_downstream`, `local_cancel_upstream`, `local_events` and `pool_controls`. Each runs a real native producer bin (`framelinkavsink native-memory=true`, writing into its own pool), a first native reader, the read-lease adapter into a same-allocation ring, and a final reader bin. The relay modes and `BrokerNativeBinRelay` left the Windows-only `fl_gst_native_producer_win`; `fl_gst_native_bin_relay_win.inc` is gone. Windows renames: `gst_native_bin_relay_win` is `gst_relay_bin_win`, `gst_native_lease_relay_win` is `gst_relay_lease_win`, and `gst_native_bin_relay_{cancel,events,supervised_downstream,supervised_upstream}_win` are `gst_relay_bin_{...}_win`; the other seven keep their names.

**New Linux samples**, each the counterpart of the D3D11 one with the same rules:
- `samples/native_dmabuf_relay_pool.h`: the export guard over the native DMA-BUF producer pool (pins every unpublished reservation, refuses busy, partial, foreign or stale pools) and `createNativeDmabufRelayPool` (an in-process same-allocation ring through `flCreateChannelFromDmabufs`).
- `samples/native_gst_dmabuf_relay.h`: the GStreamer read-lease adapter. It accepts only intact FrameLink reader memory and forwards by inode.
- `samples/native_dmabuf_relay_transfer.h`: the broker-keyed transfer (explicit abstract socket and secret) over the phase 3 Linux transfer.

**Public header addition:** `gst_framelink_memory_get_dmabuf_fd` in the installed `framelink/framelink_native.h` (desktop Linux only, static inline): the borrowed fd of an intact FrameLink reader memory, or -1. It is the Linux counterpart of `gst_framelink_memory_get_d3d11_shared_handle` and `gst_framelink_memory_get_hardware_buffer`. The header is outside the Windows freeze surface; this needs Jon's approval with the merge.

Platform parts of the fixture are in the adapter: the caps feature and producer completion contract, writing a pool buffer (a D3D11 upload, or a mapped DMA-BUF write with a signalled ready fd), reader-memory identity, and open handles (`GetProcessHandleCount`) or fds (`/proc/self/fd`) for `pool_lifetime`.

**Checks:**
- Linux: all 13 pass; the 28 shared relay tests passed 20 repeated rounds under `-j8`. Negative controls: matching the first reserved slot instead of the inode fails `lease` (unrelated ring accepted) and `bin` (allocation identity); accepting a partially available pool fails the `BUSY` check; leaking one fd per pool setup fails `pool_lifetime`.
- Windows: build and exports unchanged (88, 47, 2); all 28 `gst_relay_*_win` tests pass, and 5 repeated rounds under `-j4`; full suite 181 of 181; the unchanged 060c54e4 suite against the new DLLs 180 of 180.
- The first Windows run failed `authenticated_pool`: the shared endpoint name lacked the `framelink-cpu-` prefix that the Windows pipe endpoint requires, so the positive transfer was refused (and the four negatives were failing for that reason too). The name now keeps the original prefix on both platforms.
- Android: 95 of 97 (unchanged).
