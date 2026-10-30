# Android A/V service qualification sample (M5)

This separate `dev.framelink.av` APK reuses M4's `av::runPlayback`, authenticated
GIO/GDP PCM transport, and FrameLink AHardwareBuffer backend. It does **not**
install/modify the vkrender demo. There is no new PCM protocol.

## Supported scope

- One app UID, separate producer and receiver processes; both services unexported.
- App-private authenticated rendezvous directory with a new UUID per service run.
  This prevents restarting against a previous process's stale endpoint.
- 160x90 BGRA, 50 fps requested, 48 kHz stereo PCM synthetic 150-frame fixture.
- Real OpenSLES audio sink; its selected clock also schedules video callbacks.
- Video maps the received BGRA buffer, converts to RGBA, and posts a Surface.
  This is explicitly a CPU-copy qualification adapter, **not GPU-native**, and
  latest-wins delivery may skip many frames. `presented` is successful window
  post calls, not proof of scanout. The scheduler drops frames over 40 ms late.
- Bound foreground-only playback. Leaving the Activity, destroying its Surface,
  losing audio focus, or removing an audio route cancels the session. Reopen the
  Activity to negotiate a fresh session. Focus gain alone does not auto-resume.
- The debug fixture producer is a separate process; stopping its service kills
  that process to bound CLI calls. This is not a production reusable producer API.
- Audio focus is released on completion or service destruction. Route callbacks
  and private files are cleaned up. The native receiver has bounded connection
  waits; cancellation during a connection can take up to its 15-second timeout.

## Not advertised

No generic cross-APK A/V path, Binder broker, background/foreground service
playback, arbitrary formats, external decoded pipeline, guaranteed 50 fps,
physical lip-sync, output-device switching recovery, or Android GPU-only A/V
renderer parity. SELinux app isolation and app-private rendezvous cannot be
bypassed by merely adding a signature permission. A future cross-app broker must
provide authorized endpoint/handle delegation rather than sharing private paths.
Existing renderer Binder Surface video transport is a different API, not this
sample's A/V transport. These are remaining platform integration tasks, not passes.

## Build

Requirements: SDK 34, NDK 28.2.13676358, CMake 3.22.1, JDK 17+, Gradle 8.x,
GStreamer Android universal SDK. This module defaults to arm64.

```sh
printf 'sdk.dir=/path/to/Android/Sdk\n' > local.properties
gradle :app:assembleDebug -PgstAndroidRoot=/path/to/gstreamer/arm64
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.framelink.av/.MainActivity
adb logcat -s FrameLinkAV
```

The native wrapper also builds separately for x86_64 by passing that ABI and its
GStreamer architecture root to CMake using the NDK toolchain. Emulator runtime
has not been qualified in this change. Static registration includes FrameLink,
GIO/GDP, core/app, audio conversion/resampling/volume, and OpenSLES.

## Tests

`qualify.py --serial SERIAL --output DIRECTORY` runs normal playback, background
cancellation, a debug-only competing AudioManager focus request, reopen, and
Activity/Surface recreation. It clears device logcat between cases. It uses only
this qualification APK and finishes by force-stopping it. Debug focus-test service
is omitted from release builds. See `docs/m5/android-evidence` for device results.

Manual cable/Bluetooth route changes, hardware acoustic/scanout measurement,
Pi and Cuttlefish runtime remain unverified; no attached matching hardware was
used. These cases must not be represented by a simulated callback as hardware passes.
