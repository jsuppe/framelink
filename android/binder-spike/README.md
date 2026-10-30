# M5 Android Binder socket/AHardwareBuffer feasibility spike

**Isolated test applications, not a production broker or A/V implementation.**
This answers whether the existing native FrameLink socket/AHB protocol can work
across Android app UIDs if socket endpoints are granted by Binder. It changes no
portable FrameLink API and adds no PCM protocol.

## Experiment

- Broker declares a signature-level Binder permission and checks it again in
  every method (protecting against leaked Binder handles).
- Native `socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC)` creates connected
  endpoints. Binder transfers one as a `ParcelFileDescriptor` to a different UID.
- Broker allocates an AHardwareBuffer, writes a pixel, and sends its handle using
  `AHardwareBuffer_sendHandleToUnixSocket`—the existing FrameLink Android mechanism.
- Client maps/verifies/changes the pixel; broker reads that change from its own
  original allocation and acknowledges. This proves shared backing, not a copy.
- Grants are keyed by calling UID and owner Binder. Replacement retires the old
  grant; a stale owner's revoke cannot close the new grant. A remote owner token
  in a third process proves Binder-death revocation independently of closing the
  client's socket. Production policy/versioning/limits are intentionally absent.
- FD close ownership is singular: revoke does shutdown, the transport worker
  closes only after it finishes. New traffic has five-second socket deadlines.

## Build and test

SDK34 / NDK28.2.13676358 / CMake3.22.1 / Gradle8.x / Java17. No GStreamer dependency
is needed for this transport feasibility spike. Broker/client use the normal
shared debug signer. Generate an **independent test-only signing key** for impostor:

```sh
printf 'sdk.dir=/path/to/Android/Sdk\n' > local.properties
keytool -genkeypair -keystore /tmp/fl-spike-untrusted.jks \
  -storepass test-only -keypass test-only -alias spike \
  -dname CN=UntrustedSpike -keyalg RSA -validity 30
gradle assembleDebug -PimpostorStore=/tmp/fl-spike-untrusted.jks
python3 qualify.py --serial DEVICE --output /tmp/binder-results
```

The script installs three isolated packages, clears logcat, runs five gates, and
force-stops only these packages afterward. Wrong-signer test must fail if the
impostor was accidentally built with the normal debug key. No keystore is committed.

## Interpretation

Passing establishes a feasible **connected-FD Android backend extension**, not a
working cross-app FrameLink A/V session. The parent still needs:

1. Explicit fd adoption APIs with ownership, type/connection validation and bounds.
2. Broker-issued session/control/audio endpoints instead of app-private path lookup.
3. Existing GDP/GIO audio over the authorized endpoint, preserving M4 timing/auth.
4. Session-generation checks, stale grant rejection, bounded allocations and reset.
5. End-to-end two-app A/V negotiation/playback and death/restart tests.

**Revocation does not invalidate an AHardwareBuffer already duplicated to a peer.**
Shutdown revokes future protocol traffic; replacement generations must retire old
allocations rather than writing new private content into previously granted memory.
This spike releases its AHB before revocation and does not claim remote-memory erasure.
