# M6 step 4: Linux/Android integration against the frozen Windows API — 2026-11-19

Checked revision: `4a944ab9` (framelink `master`, after M6.9 on Linux and Android and the M6.8b/8c work, PRs #17–#22). Frozen reference: `060c54e41bcbfe7ca8dbdef89e6e74fb2930935c` (tag `m6-windows-api-freeze`, [API freeze](../API_FREEZE_CANDIDATE.md)). This record is dated; current status is in [STATUS.md](../../STATUS.md).

## Result

**No breaking change, and no change at all to the frozen Windows surface apart from two additive macros.** Nothing needs Jon's decision under the freeze's compatibility promise. The observations at the end are for the Linux/Android API decision, not for the Windows freeze.

| Check | Result |
|---|---|
| Public C headers (`include/*.h`) | Unchanged since `060c54e4` |
| Session headers preprocessed as Windows (`framelink_memory_policy.h`, `framelink_session_transport.h`, `framelink_session_context.h` plus all of `include/`) | Identical declarations, structs and enums. Two new macros: `FL_GST_LOCAL_NATIVE_SESSION` (1) and `FL_GST_LOCAL_NATIVE_RESOURCE` (`FL_MEMORY_D3D11`). Additive |
| Same headers preprocessed as desktop Linux and Android | Gain `fl_gst_session_open_local_native` and `fl_gst_session_open_local_native_epoch` (and the `FlGstNativeEpochFunc` typedef), with prototypes identical to Windows. The macros resolve to `FL_MEMORY_DMABUF` and `FL_MEMORY_AHB` |
| Windows exports, `dumpbin /exports`, both revisions built side by side on the qualification host (RTX 5090) | Identical names: `framelink.dll` 88, `framelink_gst_session.dll` 49, `gstframelink.dll` 2 ([before](windows-exports-060c54e4.txt), [after](windows-exports-4a944ab9.txt)) |
| Windows element surface, `gst-inspect-1.0` of all six elements, both revisions | Identical apart from version and filename lines ([element-surface.txt](element-surface.txt)) |
| `060c54e4`'s full Windows test suite run against `4a944ab9`'s three DLLs (test executables and clients unchanged, DLLs replaced and hash-checked) | **180/180 pass** (671 s). This extends the freeze's 3-client compatibility check to every test of the frozen revision |
| Windows source paths touched since the freeze | Reviewed. Changes are additions under `#ifndef _WIN32` / `FL_GST_DMABUF` / `__ANDROID__` / `FL_GST_EGL_READBACK`, guards widened from `_WIN32` to `FL_GST_LOCAL_NATIVE_SESSION` (defined on Windows, same code), one internal property enum renumbered in `framelinkavsrc` (GObject property IDs are not public), and `gpuReadback` checks that are always false where the property does not exist |
| Windows build | CI now configures through `cmake --preset ci` (Ninja) instead of the Visual Studio generator. Build tooling, not API |

## Linux and Android against the Windows surface

Symbols were taken from the Linux shared libraries (`nm -D`) and the Android static libraries (`llvm-nm`, defined `fl*` text symbols); lists: [linux-exports-4a944ab9.txt](linux-exports-4a944ab9.txt), [android-symbols-4a944ab9.txt](android-symbols-4a944ab9.txt).

**Core (`framelink`).** 68 functions are common to all three platforms. Every difference is a platform resource or IPC primitive with a counterpart on each platform:

| Windows only | Linux | Android |
|---|---|---|
| D3D11 images, handles, pool snapshots and the device owner (18 functions) | DMA-BUF: `flImageDmabuf`, `flCreateChannelFromDmabufs` | AHardwareBuffer: `flImageAHardwareBuffer`, `flCreateChannelFromHardwareBuffers`, `flCreateChannelFromSocketFd[WithHardwareBuffers]`, `flOpenProducerFromSocketFd` |
| `flCompleteReadLeaseD3D11Fence` | sync-fd: `flCompleteReadLeaseSyncFd`, `flFrameReadySyncFd`, `flReleaseSyncFd`, `flSignalConsumedSyncFd`, `flSubmitSyncFd` | same sync-fd set |
| `flSharedCpuOpen{Producer,Consumer}Pipe` | `flSharedCpuOpen{Producer,Consumer}Fd[Ex]` | same |

**Session (`framelink_gst_session`).** 34 C functions are common.
- Windows only: the native relay family (`open_local_native_relay`, `_relay_input`, `_relay_pool`), the FAR5 exact-pool receiver (`open_local_native_pool_receiver`, which imports D3D11 shared handles), the named-pipe CPU endpoints (`bind_authenticated_cpu_pipe`, `bind_shared_cpu_pipe`, `open_named_cpu_pipe`) and `set_d3d11_device_owner`.
- Linux and Android only: `bind_shared_cpu_fd` and `create_cpu_endpoint` (the descriptor counterparts of the pipe endpoints).

**Elements.** Linux has every Windows property and signal name, plus `gpu-readback` on `framelinkavsrc` (EGL builds) and `acquire-native-buffer` / `set-native-ready-fd` on `framelinkavsink` (DMA-BUF and Android builds). `set-local-d3d11-device-owner` is registered everywhere and returns FALSE off Windows. The Android plugin is static and was checked from source only: the same `framelinkavsink` additions, no `gpu-readback`.

## Observations for the Linux/Android API decision (not Windows-breaking)

1. **No native relay or exact-pool receiver on Linux/Android.** Those session entry points exist only on Windows. **Decision (Jon, 2026-11-19): full parity.** Linux and Android get the FAR4 relay entry points and a FAR5 same-allocation equivalent (consumer-pool snapshot, authenticated pool transfer, `set-local-native-pool-candidate`, samples, the Windows relay tests' counterparts and two-hop qualification with PCM), Linux first. This is M6.9d work; all of it is additive off Windows.
2. **Linux shared libraries export internals.** `libframelink.so` exports `fl::Channel` / `fl::Listener` C++ symbols (20), and `libframelink_gst_session.so` exports `av::` rendezvous and authentication helpers (21), because the Linux build uses default symbol visibility. Windows exports only `FL_API` functions. Hiding them (`-fvisibility=hidden`) is a Linux ABI decision to take before a Linux/Android freeze. **Decision (Jon, 2026-11-19): hide them** (branch `feature/linux-symbol-visibility`): the three libraries export only what the headers mark for export, giving 80 / 43 / 2 on Linux, and `tools/check_exports.py` checks the Linux and Windows export lists against committed baselines in CI.
3. **Seven session functions are C++-mangled on both platforms** (`publish_audio`, `publish_events`, `peek_video_end`, `open_local_av_epoch`, `open_local_cpu_epoch`, `local_ready_cancellable`, `finish_local_cancellable`). They are declared in the internal `fl_gst_session_internal.h`, not a public header, and were already exported mangled at the freeze. **Decision (Jon, 2026-11-19): option C.** The two library-only functions are no longer exported, `peek_video_end` gets a C name, and the epoch and cancellable variants become public C API on every platform; see [API freeze, changes after the freeze](../API_FREEZE_CANDIDATE.md#changes-after-the-freeze).
4. **`flReconfigure` on Linux** is an undeclared stub (since 2026-08-10) that returns `FL_INTERNAL`. Not in any public header; not exported on Windows. **Decision (Jon, 2026-11-19): remove it** (branch `feature/remove-flreconfigure-stub`). Nothing calls it, and `flRequestGeometry` already covers size and format changes on every platform. The Linux core library now exports 79 functions and no longer carries one that Windows and Android lack.
5. **`gpu-readback` registration differs between elements.** `framelinksrc` registers it on every platform (it is in the frozen Windows surface and refused there); `framelinkavsrc` registers it only in Linux EGL builds. Registering it on the A/V source everywhere would be additive for Windows. **Decision (Jon, 2026-11-19): register it on `framelinkavsrc` everywhere** (branch `feature/avsrc-gpu-readback-everywhere`); builds without the EGL adapter refuse it at start. This is the convention for platform-dependent element features; see [API freeze, changes after the freeze](../API_FREEZE_CANDIDATE.md#changes-after-the-freeze).
