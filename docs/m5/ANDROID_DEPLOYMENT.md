# Android M5 task breakdown and evidence

## Implemented initial bounded increment

- Package the actual M4 authenticated A/V receiver in an isolated APK: implemented.
  Compile the existing `av_apps.cpp`, authentication, rendezvous and FrameLink
  Android backend; register static GStreamer plugins. No new audio transport.
- Native deployment: arm64 and x86_64 builds pass. APK arm64 debug builds.
- Ownership/permissions: unexported same-app producer/receiver services, separate
  producer process, private per-run authenticated rendezvous. No media permission
  is needed for generated playback; INTERNET permits the local GIO TCP connection.
- Actual receiver: OpenSLES audio plus received BGRA Surface rendering on the
  common selected clock. CPU-copy qualification path, not GPU-native parity.
- Audio focus/background: cancellation terminates both tracks; explicit reopen
  creates a fresh identity. Focus gain does not resume old timestamps.
- Surface recreation: old worker cancellation and serialized restart preserve
  window ownership. Cancellation may wait for the bounded connection timeout.
- Audio-route removal: stop/reopen policy implemented; hardware route change
  remains untested, not simulated as passing.

## Validation

Device: TCL 8483A tablet, Android 15/API35, arm64 (see evidence/device.txt).
Evidence under `android-evidence/` is captured from the isolated qualification APK.
Normal playback completes 144000 PCM samples and producer returns zero. Video is
latest-wins: only a subset is posted, explicitly not a lossless 50fps qualification.
`audio_master=1` indicates OpenSLES sink clock selection, not acoustic/scanout sync.
Debug competing AudioManager focus request tests an actual framework callback.
Final on-device matrix: **5/5 passed** (normal, background, focus, reopen,
Surface recreation). An external shell attempt to start the production receiver
service was rejected as not exported. Final teardown left no app audio-focus entry.
This is qualification of the explicitly limited sample mode, not all Android tasks.

Initial testing exposed a stale rendezvous race on app restart; unique per-service
session directories fixed it. The first recreation harness ended its observation
window before replacement playback finished; the final harness allows startup plus
three seconds of playback. Neither initial failure is counted as a pass.

## Remaining tasks (do not close full Android parity from this increment)

1. Authorized cross-app broker/session endpoint delegation. Existing BufferQueue
   Binder video API does not carry this A/V session; Unix-socket/private-filesystem
   sharing cannot be assumed between APK UIDs. Define security before implementation.
2. GPU-native renderer shared-clock scheduling and audio output integration (this
   sample deliberately uses CPU Surface conversion).
3. Background playback product policy and foreground media service lifecycle;
   current supported mode cancels on Activity departure.
4. Test actual route changes, device absence, unsupported formats and suspend/resume.
5. Pi/Cuttlefish runtime when available; x86_64 compilation is not VM validation.
6. Physical acoustic/display measurement if required; separate from transported PTS.

The existing GStreamer path is viable on-device. No measurement presently
justifies replacing it with a custom PCM pipe. Additional deployment work remains.

## Cross-APK feasibility follow-up

`android/binder-spike` adds isolated broker/client/impostor APKs. On the same API35
physical tablet with SELinux **Enforcing**, all **5/5** gates passed:

- Different-UID AHardwareBuffer shared-pixel roundtrip and explicit socket revoke.
- Client process death cleanup.
- Independent Binder owner-token process death while client retains socket: EOF.
- Replacement grant remains usable after stale-token revoke.
- Independently signed client rejected before any grant.

See `binder-spike-evidence/`, including distinct signing certificate digests.
This clears the connected-FD architecture feasibility question. It does **not**
complete cross-app A/V integration: Android fd adoption and session endpoint
provider APIs, protocol generation/lifetime rules, and end-to-end A/V tests remain.
No new PCM transport was introduced; the spike tests the existing native
AHardwareBuffer socket transfer primitive only. Revocation cannot invalidate
previously transferred buffer handles; new generations must retire old allocations.
