# Android native connected-video bridge — 2026-11-01

Source `3ac38ad`, parent implemented/reviewed, not delegated. Release builds for
Android arm64-v8a and x86_64/API29 succeeded. Linux Release build succeeded;
`ipc_connected_fd`, `ipc_expanded_capacity`, `roundtrip`, `sample_bgra` passed4/4.

On A06B4AABFB07983 (Android15, SELinux Enforcing), seven sequential cases pass:
- socket adoption: reject/consume invalid FDs, CLOEXEC/blocking, SCM_RIGHTS,
  explicit revoke and finite silent-peer timeout;
- native API: consumer/producer failure cleanup, silent handshake, prompt close
  while a60s handshake is pending; RGBA/BGRA/NV12 exact pixels/PTS, held-slot and
  read-completion lifetime, geometry refusal before retirement, explicit revoke;
  a retained native AHB survives channel teardown; separate processes transfer
  32/32 independent pixel identities and exact PTS with one shared buffer;
- unchanged named path: RGBA, BGRA, NV12 patterned mapping8frames each;
- existing ready/consumed FD ownership and held-frame reconnect tests.

Raw device output and revision are saved alongside this file. The injected pipe/
eventfd readiness signals test ownership, not actual GPU SYNC_FD completion.
This is same-UID native qualification (including separate processes), not signed
cross-UID A/V. No global policy/settings/logcat changes; test artifacts only in
/data/local/tmp/fl-m5-connected-20261101. New connected entry points bypass named
lookup, not authorization: the broker must authenticate and revoke its leases.

## Reproduce

Build core with NDK28.2/API29, FRAMELINK_BUILD_TESTS=ON, GST/SAMPLES=OFF, then
build `fl_android_connected_fd`, `fl_ipc_adopt`, `fl_portable_map`,
`fl_sync_fd_test`, `fl_reconnect_held`. Push binaries plus matching
libc++_shared.so; run each with that directory in LD_LIBRARY_PATH. Every test is
bounded by an outer15s timeout. See summary.json and per-case logs.

## Next

Bind fresh signed broker leases to producer/playback SessionProviders and native
GStreamer video opens. Qualify actual cross-UID video+GDP audio, exact PTS/PCM and
producer/consumer/broker death/restart. M5 remains incomplete and unmerged.
