# M6.9 on Android: native AHardwareBuffer producer in coordinated A/V sessions — 2026-11-18

Base revision: `fb922377` (framelink `master`, after the Linux step, [linux-native-av-session](../linux-native-av-session/README.md)). This record is dated; current status is in [STATUS.md](../../STATUS.md).

## What changed

- **Session profile on Android.** The explicit local native session profile and `set-local-native-candidate` on both A/V bins are now enabled on Android. `FL_GST_LOCAL_NATIVE_RESOURCE` is `FL_MEMORY_AHB` there. The candidate rules are the same as on Windows and Linux, with no fallback.
- **Windows-only parts stay Windows-only.** The relay, exact-pool receiver and device-owner entry points return false on Android.
- **`framelinkavsink` video pad** advertises `video/x-raw(memory:FrameLinkAHardwareBuffer)` for RGBA and RGB10A2_LE, the same formats as `framelinksink` on Android. Android native output is exact RGB only, with no NV12.
- **Producer actions.** `set-native-ready-fd` and `acquire-native-buffer` were already forwarded by the bin on Android builds, from the Linux step.
- **No change to Windows, Linux, the wire protocol or defaults.**

## Test

The Linux test became one source, `test/fl_gst_native_av_session.cpp`, with a small platform layer. It is registered as `gst_native_av_linux` and `gst_native_av_android`.
- On Android it writes source pixels by locking the AHardwareBuffer.
- It checks allocation identity with `AHardwareBuffer_getId`.
- It reads native output by locking the buffer for CPU read.
- It registers the plugins statically.
- The second format case is RGB10A2_LE instead of NV12.

Output on the TCL 8483A tablet (Android 15, arm64, vendor GPU), built with NDK 28.2 and the GStreamer 1.24.13 Android SDK:

```
native bins (CPU readback receiver): 28 configuration controls, pool-receiver rejection and both-role bounded cancellation pass
native local setup (CPU readback receiver): positive, invalid policies, foreign resource, candidate mismatch, plain-profile isolation, capacity, epoch and cancellation pass
native bins (native receiver): 28 configuration controls, pool-receiver rejection and both-role bounded cancellation pass
native local setup (native receiver): positive, invalid policies, foreign resource, candidate mismatch, plain-profile isolation, capacity, epoch and cancellation pass
RGBA native AHardwareBuffer A/V bins, native receiver: 8 frames, original allocations, pixels, PCM and A/V offset pass
RGB10A2_LE native AHardwareBuffer A/V bins, native receiver: 8 frames, original allocations, pixels, PCM and A/V offset pass
RGBA native AHardwareBuffer A/V bins, native receiver, sample+render producer: 8 frames, original allocations, pixels, PCM and A/V offset pass
RGBA native AHardwareBuffer A/V bins, CPU readback receiver: 8 frames, original allocations, pixels, PCM and A/V offset pass
PASS
```

## Regressions

| Platform | Result |
|---|---|
| Android tablet, all 96 CTest-registered Android tests run on the device | 95 pass, 1 skipped (`read_lease_egl_delayed_completion`, unsupported control) |
| Linux (Ubuntu 26.04, RTX 3090, GStreamer 1.28.2), full suite | 141/142 pass, 1 skipped (same test) |
| Windows (RTX 5090 qualification host), `cmake --preset ci`, full suite | 180/180 pass |

How the Android suite was run:
- Each test's executable was pushed and run with its registered arguments, in its own scratch directory, honoring its skip code and timeout.
- `roundtrip` is driven by a host CMake script, so its consumer and producer ran directly on the device.
- `gst_av_bench_smoke` writes its report to the host build directory. It ran with `--out .` (the scratch directory) and passed.

## Not established here

- **9d:** real decoder or renderer producers and relay paths on Android, and producer GPU writes with real fences. This test writes through a CPU lock and signals an already-complete eventfd.
- **9e:** final-revision gates.
- No claim of physical presentation timing or arbitrary decoder compatibility.
