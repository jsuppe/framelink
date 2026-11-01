# Signed cross-application A/V example (Android 10+)

Three APKs exercise **the real FrameLink video channel and GDP PCM transport**:

- `broker`: signature-authorized Binder grant service, with a separate
  `:producer` process running `runBrokerProducer`.
- `client`: a different Android UID, running `runPlayback`, validating original
  video pixels/PTS and every decoded PCM sample/PTS, then posting to a Surface.
- `impostor`: identical client code signed with a different test certificate;
  it must be rejected before receiving descriptors or secrets.

This is a bounded foreground qualification example, **not a zero-copy GStreamer
pipeline**. Native video allocations are shared AHardwareBuffers; current Gst
system-memory caps map/copy pixels. NV12 is converted to RGB on the consumer CPU.
GDP serializes PCM. M5a owns native caps/allocation negotiation and copy removal.

## Build and run

Requires Android SDK34, NDK28.2.13676358, CMake3.22.1+, Gradle8.14 and the
GStreamer Android distribution (arm64). Use a separate **test-only** signer:

```sh
keytool -genkeypair -keystore /tmp/fl-untrusted.jks -alias spike \
  -storepass test-only -keypass test-only -keyalg RSA -validity 30 \
  -dname 'CN=FrameLink test impostor'
gradle -p android/crossapp assembleDebug \
  -PgstAndroidRoot=/path/to/gstreamer-android/arm64 \
  -PimpostorStore=/tmp/fl-untrusted.jks
```

Install each module's `build/outputs/apk/debug/*-debug.apk`, then:

```sh
adb shell am start -n dev.framelink.cross.client/dev.framelink.cross.Viewer \
  --es format NV12 --ei frames 90 --ei fps 30 --ez silent true
python3 android/crossapp/qualify.py --serial SERIAL --out /tmp/crossapp-evidence
```

BGRA/NV12, 160x90, stereo S16/48kHz, 440Hz test tone; `silent=false` selects the
actual OpenSLES sink. Supported request bounds: 1..18000 frames, 13..120fps,
duration <=300 seconds. These bounds are **not a throughput qualification**.
The harness qualifies 30fps and verifies accepted Surface posts, not scanout.
The explicit `--es clock-policy producer|audio` control selects the core
ProducerMapped or AudioPreferredCoordinated protocol; producer remains the
example default pending qualification. Both use the bounded PRIMED/GO barrier
and finite-session budget. `audio` is now mode5 (mode4 without a finite budget),
not legacy mode1: the producer cannot publish video while the receiver is still
opening its sink. Both tracks use the same device-clock epoch, with explicit
system fallback if the sink provides no clock. PTS are never rewritten, and no
automatic policy switch occurs. Old producers reject these new modes explicitly.
The harness accepts the same selection as `--clock-policy`. Debug
`preroll-delay-ms` uses the existing bounded startup fault hook; it is ignored in
a non-debuggable APK. Previous legacy-mode failed controls remain in the evidence.
The 100ms common playback latency is a final-output scheduling policy, not a
per-hop FrameLink buffer-fill requirement. Pool capacity4 does not imply four
frames of transport latency.

## Ownership and authorization

Each grant creates distinct connected control/audio STREAM and native-video
SEQPACKET endpoints, a fresh random secret, UUID and broker-local generation.
The signature permission is checked at binding and each grant/revoke call.
The broker is the authority; inherited socketpair peer credentials are **not**
used to identify the delegated client. No global socket names, permissive chmod,
SELinux relaxation, or shared Android UID are required.

Binder owners and stable duplicate descriptors retain revocation authority.
Explicit revoke, replacement, peer death, surface loss, background departure,
audio-focus loss and audio-route removal shut down both media tracks. A new
grant is required; old timestamps are not resumed. Old owner revocation cannot
close a replacement grant. Broker death invalidates outstanding endpoints in
both native peers. Native channels consume their descriptors and retain GPU
allocation ownership independently of a retired lease.

The example supports one selected consumer/session at a time. Same-owner active
reacquisition is rejected; a new owner token replaces a same-UID lease. Different
UID replacement is refused. Background playback is deliberately unsupported.
Generation numbers are broker-process local; globally fresh UUIDs/secrets, not
generation alone, distinguish restarts. This is not a persistent registry.

Debug-only death/focus receivers are absent from release manifests. They can
only affect these test apps; never deploy debug builds as a hardened broker.
Production apps need their own signing, user-selection and lifecycle policy.

## Qualification and known limit

On the TCL8483A Android15 device, signed cross-UID BGRA/NV12 payload/PTS,
rejection, replacement and independent peer-death/restart tests passed. Real
AudioManager focus loss stopped both tracks; explicit reopening passed.
See [raw evidence and precise scope](../../docs/m5/crossapp-20261101/README.md).

**OpenSLES ProducerMapped playback is intermittently failing** on this device:
several early runs stalled PCM admission and failed the existing deadline or
completion guard. Later untraced/longer runs and the full matrix passed, but no
root-cause fix has been established. Do not treat those passes as removal of
this limitation or as M5 completion. Silent-sink transport validation is
separate. Real route unplug, physical acoustic/scanout alignment and suspend are
not qualified. x86_64 compilation does not establish emulator runtime support.

## Diagnostics

`--ez diagnostics true --ei diagnostic-level 0` enables bounded-rate clock/queue
records. Level5 also enables verbose GStreamer sink logging; level-1 buffers
up to2048 diagnostic strings and prints them after the session. Normal operation
does not collect these diagnostic records. Failure-only calibration is reported
before teardown. Observed logging-sensitive behavior is retained in evidence.
The PCM observer is opt-in, runs on the audio streaming thread and must be
bounded/thread-safe; it may not modify the borrowed buffer or its timestamps.
Observer exceptions invalidate the session rather than cross a C callback.
