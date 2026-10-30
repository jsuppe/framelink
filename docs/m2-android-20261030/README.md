# M2 Android session contract validation — 2026-10-30

Source: `2839525` (session contract integrated on M1 baseline). Session API/test originated in `0a5ba51`.

## Final review-fix qualification

Revalidated code `fddbf7c`, cherry-picked as `cfca345` on the same baseline.

- Full native incremental/reconfigured build: arm64-v8a and x86_64 PASS.
- `fl_session_contract`: PASS on Pi and tablet, including selection validation and stable track-ID replacement rules added by review.
- `fl_session_c_api`: PASS on Pi and tablet, validating the public API from a C11 translation unit linked against the C++ implementation.
- Exact output is in `*-final-build.log` and `*-final-runtime.log`.
- x86_64 remains compile-only. Desktop independent-peer test is not registered for Android and is not claimed here.

## Original baseline results

- Full native CMake release build, arm64-v8a: PASS.
- Full native CMake release build, x86_64: PASS (compile only; no x86_64 guest runtime executed).
- Built `fl_session_contract` on physical Pi 5: PASS.
- Built `fl_session_contract` on physical Android tablet: PASS.

NDK 28.2.13676358, API 29, C++ static runtime. The complete default Android native target set was built, including the `framelink` static library and session contract test linked against it; this was not merely a standalone compilation of the helper source. GStreamer was disabled for this core build.

The runtime checks cover configuration validation, capability negotiation, equivalent rational video rates, incompatible formats, generation replacement, descriptor encoding/decoding and malformed sizes, reserved bytes/version checks, checked clock mapping, overflow rejection, and sample offset calculations including 44.1 kHz cumulative timing. See `test/fl_session_contract.cpp` for exact assertions.

## Reproduction

```sh
cmake -S . -B /tmp/fl-m2-android-arm64-v8a \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static -DFRAMELINK_BUILD_GST=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/fl-m2-android-arm64-v8a -j8
# Repeat with ANDROID_ABI=x86_64 and a separate build directory.
adb -s "$DEVICE" shell mkdir -p /data/local/tmp/fl-m2-session-20261030
adb -s "$DEVICE" push /tmp/fl-m2-android-arm64-v8a/fl_session_contract /data/local/tmp/fl-m2-session-20261030/
adb -s "$DEVICE" shell chmod 755 /data/local/tmp/fl-m2-session-20261030/fl_session_contract
adb -s "$DEVICE" shell /data/local/tmp/fl-m2-session-20261030/fl_session_contract
```

## Scope and limits

This validates CPU session-contract helper semantics and Android library integration. It does **not** validate independently negotiated producer/consumer sessions, hardware playback, audio/video synchronization, actual 44.1 kHz playback, GPU buffers, or Cuttlefish. No APK install, UI/lifecycle change, or live demo modification occurred. Only an isolated executable under `/data/local/tmp/fl-m2-session-20261030` was installed on each device.

Build and device output logs are adjacent to this file.
