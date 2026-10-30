# Windows M3 two-process qualification — 2026-10-30

Isolated Caspar checkout: `C:\Users\jonsu\dev\framelink-m3-20261030`.
The existing interactive renderer demo was not modified. No physical playback
or speaker/display alignment qualification is claimed by these headless tests.

## Revision and toolchain

- Runtime/auth/rendezvous base: `3074e2a`.
- Expanded ten-case harness: `629a6f2` (local equivalent `41e34b6`).
- Required MSVC enum narrowing fix: `a2977e4`.
- Added CPU auth/rendezvous CMake registration: `2932696` (local `19bef11`).
- Visual Studio 2022 / MSVC 19.44.35216.0; Windows SDK 10.0.26100.0.
- Explicit GStreamer root: `C:\Program Files\gstreamer\1.0\msvc_x86_64`, installed 1.28.
- Release x64. GStreamer bin directory added to PATH for runtime tests.

## Results

- Initial build failed on an enum-to-uint32_t aggregate narrowing conversion.
  The failure is preserved; explicit cast fix rebuilt both A/V apps successfully.
- Existing complete native CTest suite: **34/34 passed**, 21.75 s.
- New protected-rendezvous and mutual-authentication tests: **2/2 passed**.
  These cover Windows owner exclusion, crash recovery, malformed metadata,
  NULL-DACL rejection, valid auth, wrong key/role, fake server and reflected proof.
- Independent two-process acceptance matrix: **10/10 passed**:
  BGRA/NV12 in both startup orders, producer restart, consumer restart,
  producer replacement changing BGRA to NV12, missing audio, audio timestamp
  reset, and producer death. Successful sessions verify 50 video frames and 50
  PCM blocks, timing and matching sender/receiver session identities.
- Negative cases require nonzero consumer exit with specific error evidence;
  fault detection is not represented as fault concealment or audio recovery.

`runtime.log` contains suite and matrix output. `m3-evidence/` retains each
peer's output and per-case JSON result. `security.log` records both CPU tests.
No physical playback, device switching, long-term clock drift, or Android APK
qualification is implied; those remain later roadmap work.

## Final integrated-source rerun

Added CLI/CI registration `db2fbf4` and review cleanup fix `9d1eed6` on top of
all changes above (local final source `eaaa38f`). Rebuilt all targets and ran
**39/39 CTest tests successfully**, 50.08 seconds. This includes both CPU
security gates, both help gates and the full ten-case `m3_av_apps` matrix.
`final-39-tests.log` and `final-LastTest.log` retain the final build/test evidence.
