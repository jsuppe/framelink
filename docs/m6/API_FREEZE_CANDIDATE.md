# M6 Windows API freeze (M6.7 item 7.1) — FROZEN at `060c54e4` (Jon, 2026-11-17)

Jon approved this freeze on 2026-11-17, with D2/D3 deferred. This page records the exact public surface, what compatibility it promises, the evidence, and the deferred decisions. The evidence below was collected at the proposed revision; one duration cell failed (see the table).

## Proposed revision
- Revision: **`060c54e41bcbfe7ca8dbdef89e6e74fb2930935c`**, which is `a50841a` plus the option B device owner, its fault test, the header typedef guard and documentation. [Final integrated evidence](windows-integrated-final/README.md).
- `FL_VERSION` stays **6**, and the producer/consumer wire protocol is unchanged since `a50841a`. Both peers must still run the same `FL_VERSION`.

## Public surface
| Layer | Surface | Change since `a50841a` |
|---|---|---|
| C core (`framelink.dll`) | `framelink.h`, `framelink_d3d11.h`, `framelink_memory.h`, `framelink_session.h`, `framelink_slots.h`, `framelink_shared_cpu.h`, `framelink_shared_cpu_channel.h`, `framelink_shared_cpu_win.h` (`framelink_trace.hpp`: public status to confirm; the Linux/Android headers `framelink_dmabuf.h`, `framelink_ahb.h`, `framelink_sync_fd.h` are outside this Windows freeze) | **+8 functions** (device owner: create, retain, release, device, adapter, channel create, channel from handles, bounded producer open); 80 → 88 exports |
| Session (`framelink_gst_session.dll`) | `framelink_session_transport.h`, `framelink_session_context.h`, `framelink_memory_policy.h` | **+1 function** `fl_gst_session_set_d3d11_device_owner`; 48 → 49 exports |
| GStreamer plugin (`gstframelink.dll`) | `framelinksink`, `framelinksrc`, `framelinkavsink`, `framelinkavsrc` | **+1 action signal** `set-local-d3d11-device-owner` on both A/V elements; properties and defaults unchanged |

All changes are additive. No existing function signature, struct layout, enum value, property, default or element changed. The duplicate `flD3D11DeviceOwner` typedef is guarded so that both headers can be included from C.

## Compatibility promise if frozen
- **Kept:** every exported name, signature, struct layout and enum value in the surface above. The same applies to element names, properties, signals and their defaults.
- **Not promised:** export ordinals (use named imports or runtime lookup), unlisted or internal headers, and test/bench tools.
- **A breaking change after the freeze** needs a new `FL_VERSION` and a documented migration.
- **Behaviour notes for callers** are unchanged from the app contract. An explicit missing adapter returns `FL_ADAPTER_MISMATCH`, and producer-open failure clears its output. See [WINDOWS_APP_CONTRACT.md](WINDOWS_APP_CONTRACT.md).

## Evidence required (status)
| Check | Status |
|---|---|
| Hosted CI at this revision (PR #10) | **pass**, Linux and Windows, including the GStreamer CPU tests |
| Windows full serial CTest, fresh Release build (previous 178 tests + two owner tests) | **180/180 pass**, 0 skipped (adds `device_owner_faults_win`) |
| Exports: 128 baseline retained, exactly +9, nothing unexpected | **pass** (88 + 49 = 137) |
| C11 translation unit including both owner headers | **pass** |
| 3 unchanged `7b8a274`-built clients against the new DLLs | **3/3 pass** |
| Option B lifecycle, advertised native configurations | **pass**: 6/6 advertised cases (3.16–13.97 KiB/cycle) plus BGRA1080p60 3/3 and held leases |
| 1800 s duration on this revision, shared CPU (4 cells) and native 4K30 (2 cells) | **pass**, 6/6, zero deadline misses. [Results](windows-final-duration-060c54e4/README.md) |
| 1800 s duration on this revision, native NV12 1080p60 | **FAILED** at about 1298.7 s: test-source wake-up 43.5 ms late, then queue-1 replacement. Same signature as the excluded BGRA 1080p60 cell. Does not involve the API surface. Jon, 2026-11-17: supported under the host-scheduling assumption, failure stays recorded |

## Changes after the freeze
- **2026-11-19 (Jon): session library interface to the plugin.** Seven functions from the internal, uninstalled `fl_gst_session_internal.h` were exported with C++ (MSVC) names; they were outside the promise ("unlisted or internal headers"). Now:
  - `fl_gst_session_publish_audio` and `fl_gst_session_publish_events` are no longer exported. They are called only inside the session library.
  - `fl_gst_session_peek_video_end` stays internal but has a C name.
  - `fl_gst_session_open_local_av_epoch`, `fl_gst_session_open_local_cpu_epoch`, `fl_gst_session_local_ready_cancellable` and `fl_gst_session_finish_local_cancellable` are **new public API** in `framelink_session_transport.h`, with the public `FlGstEpochFunc` typedef. `FlGstNativeEpochFunc` is kept as the same type.
  - `framelink_gst_session.dll` exports go from 49 to 47 (7 mangled names removed, 5 C names added). No promised name, signature, struct, enum, property or default changed. `FL_VERSION` and the wire protocol are unchanged.
  - **Pairing:** `gstframelink.dll` and `framelink_gst_session.dll` must come from the same build. A 060c54e4-era plugin does not load against the new session library (it imports the removed names). They ship together.
  - The export lists are checked in CI against `tools/exports/`.

## Deferred decisions (Jon, 2026-11-17: deferred until a convenience entry point is proposed)
- **D2:** the default for new convenience callers (strict bounded failure versus explicitly specified realtime/latest behaviour).
- **D3:** whether a new convenience entrypoint may choose a compatibility copy profile.
- **Recommendation:** the proposed surface adds **no** convenience entrypoint, so the freeze can record D2/D3 as deferred until such an entrypoint is proposed. Existing defaults are unchanged, profiles stay explicit and strict/native, and CPU/readback is opt-in.
