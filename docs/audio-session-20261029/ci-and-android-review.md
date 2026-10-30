# Final sample rebuild and CI coverage (2026-10-30)

Source: `180e0a2` (review/timestamp watchdog integrated).

## Android native qualification

Fresh Release builds succeeded for both `arm64-v8a` and `x86_64`, Android API 29,
NDK 28.2.13676358, GStreamer Android 1.24.13. All configured native targets built,
including the watchdog-enabled A/V sample. The x86_64 executable was **compiled,
not run**: no Cuttlefish session was started for this check.

The arm64 sample was copied with its matching libc++ runtime to a separate
`/data/local/tmp/fl-audio-review-20261030` directory on each device. No APK,
settings, activity, or installed demo was changed.

- Pi: normal 150 video + 150 audio passed; scheduled video lateness p99 1.727 ms.
- Tablet: normal 150 + 150 passed; scheduled lateness p99 51.434 ms. Passing data
  and timestamp checks does not establish smooth playback.
- Both fault runs: 150 video + 149 audio, one missing block, forty deliberately
  drifting blocks, one discontinuity/reset detected. All checks passed.
- Reset fault-mode lateness near two seconds is expected from intentionally
  scheduling past timestamps, not a performance baseline.

The desktop `gst_timestamp_contract` executable is not currently an Android
static target; these device results cover the native A/V normal/fault fixture,
not that additional desktop-only contract executable. Speaker, display, APK,
cross-app security and hardware audio-clock drift remain outside this check.

## CI implementation

FrameLink previously had no repository workflow. `build.yml` now builds the
complete sample/plugin/test targets on hosted Linux and Windows, runs six
CPU-only regressions, and requires registration of `gst_av_session`,
`gst_av_faults`, and `gst_timestamp_contract`. Missing GStreamer can no longer
silently omit those gates while this CI succeeds.

Hosted jobs have no qualified native DRM/D3D11 allocation environment. They
explicitly **do not claim to execute A/V GPU transport tests**. The separate
manual `av-hardware.yml` runs all three actual A/V tests on trusted self-hosted
Linux/Windows machines labeled `framelink-gpu`, with native GPU access and the
GStreamer development/runtime environment preinstalled. Register those runners
before dispatch; this change does not silently install a persistent runner on
Melchior or Caspar. Fork pull requests cannot trigger the hardware workflow.

Local workflow validation: both YAML files parsed; registration checker passed
against the real build and failed against an empty test directory; selected
CPU regressions passed 6/6. GitHub execution is pending branch publication.
Actual platform runtime evidence remains the independently recorded Linux,
Windows and Android runs, not the hosted compile-only coverage.
