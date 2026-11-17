# M6 Windows API freeze candidate (M6.7 item 7.1) — DRAFT, NOT FROZEN

The freeze is Jon's decision. This page is what he would approve: the exact public surface, what compatibility it promises, the evidence, and the decisions that are still open. The revision and evidence links are filled in after the final integrated run ([windows-integrated-final](windows-integrated-final/)).

## Proposed revision
- Revision: _pending the final integrated run_. It is `a50841a` plus the option B device owner and documentation.
- `FL_VERSION` stays **6**, and the producer/consumer wire protocol is unchanged since `a50841a`. Both peers must still run the same `FL_VERSION`.

## Public surface
| Layer | Surface | Change since `a50841a` |
|---|---|---|
| C core (`framelink.dll`) | `framelink.h`, `framelink_d3d11.h`, `framelink_shared_cpu_win.h`, `framelink_shared_cpu_channel.h` | **+8 functions** (device owner: create, retain, release, device, adapter, channel create, channel from handles, bounded producer open); 80 → 88 exports |
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
| Linux build and CI CPU subset at this source | 24/24 pass locally. The GStreamer tests need CI because this host has no GStreamer dev packages |
| Windows full serial CTest, fresh Release build (previous 178 tests + `device_owner_win`) | pending final run |
| Exports: 128 baseline retained, exactly +9, nothing unexpected | pending final run |
| C11 translation unit including both owner headers | pending final run |
| 3 unchanged `7b8a274`-built clients against the new DLLs | pending final run |
| Option B lifecycle, advertised native configurations | BGRA1080p60 3/3 pass; 6 runs for NV12 1080p60/NV12 4K30/BGRA 4K30 in progress |

## Open decisions (Jon)
- **D2:** the default for new convenience callers (strict bounded failure versus explicitly specified realtime/latest behaviour).
- **D3:** whether a new convenience entrypoint may choose a compatibility copy profile.
- **Recommendation:** the proposed surface adds **no** convenience entrypoint, so the freeze can record D2/D3 as deferred until such an entrypoint is proposed. Existing defaults are unchanged, profiles stay explicit and strict/native, and CPU/readback is opt-in.
