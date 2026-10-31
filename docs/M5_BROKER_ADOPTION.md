# Broker-to-GIO connected stream seam

This increment connects trusted platform session discovery to the existing
playback receiver without changing desktop private-directory rendezvous.
It is **not yet the Android cross-app A/V demo**.

`PlaybackOptions::sessionProvider` is optional. When supplied, both `resolve`
and `connect` callbacks are required. The adapter must outlive `runPlayback`.

- `resolve` supplies a fresh broker lease and endpoint key on each retry.
  The port is unused by this path. Never recycle a revoked lease/key.
- `connect` returns a full owning `GSocketConnection` reference, with separate
  byte-stream connections for control (`C`) and GDP audio (`A`).
- The playback receiver still authenticates each connection. Audio HMAC keys
  remain derived from the negotiated session identity and generation.
- Android JNI can detach a ParcelFileDescriptor and pass the owned descriptor
  to `av::adoptConnectedStreamFd`. This helper consumes the descriptor on every
  success/failure path, rejects non-stream or unconnected descriptors, sets
  close-on-exec and finite socket I/O timeouts. Do not pass a borrowed `getFd()`.
- Broker death/revoke should **shutdown** retained descriptor duplicates;
  closing a duplicate alone does not interrupt the adopted connection.
- Callbacks must obey their passed timeout. This is a synchronous API, not a
  mechanism that can forcibly interrupt an arbitrary blocking callback.
- AHB native handle transfer remains on a different channel; the Binder spike's
  SOCK_SEQPACKET pair cannot directly carry GDP's byte-stream protocol.

## Verified by this increment

Linux GIO adoption tests cover byte transfer, descriptor rejection/cleanup,
explicit shutdown revocation, peer-death EOF, mutual authentication, rotated
lease key rejection and wrong-role rejection. Desktop receiver/producer builds
prove the optional API does not require a provider for existing callers.

## Producer provider increment

`runBrokerProducer(ProducerOptions, error)` resolves exactly one broker lease and
consumes server C/A connections through the same provider contract. Both sides
retain the existing mutual HMAC; audio keys derive from negotiated identity and
generation. The desktop listener and optional-provider playback API are unchanged.
The provider and optional `stop` atomic must outlive the synchronous call.

Producer bounds match the current CLI: 1..100000 frames, 2..4096 x 2..2160,
13..240 fps, 8000..192000 Hz, mono/stereo, 100..60000 ms timeout; BGRA or even-sized
NV12. Default startup is 15 seconds, socket idle remains 1–2 seconds. Optional
`mediaRunMs` is 1..300000 ms and must cover declared media duration and exactly
match a mode-3 receiver; zero retains legacy whole-session timeout. Modes 0–3,
PRIMED/GO order, pool12/pending6/common100ms policy are not changed by this seam.

The embedded producer **never calls process exit** on timeout/cancellation.
Its guard uses thread-safe GLib cancellation for control/auth I/O and retained
appsrc references for EOS on failure, then unwinds the owning thread. It never
races a shutdown on the same GSocket with owning stream teardown; the broker
separately revokes via its retained descriptor duplicates. Audio output I/O
retains the existing 1–2 second bound. The CLI retains its hard watchdog.
Provider callbacks must honor the supplied timeout; arbitrary blocking provider
code cannot be forcibly cancelled. Native video setup/teardown can use the
configured startup interval. A mode-1/2 receiver missing GDP EOS can still wait
its own bounded session deadline; producer cancellation does not promise an
immediate receiver callback. No heartbeat/recovery protocol is added here.

`broker_producer_stream` exercises real HMAC/negotiation/GDP on two Unix STREAM
pairs: legacy and coordinated playback, preparation delayed beyond the idle
interval, finite playback beyond startup timeout, malformed bounds, missing
peer, wrong root/audio keys, wrong audio role, budget disagreement, revoke,
cancellation and deadline. Session/clock/end ordering is asserted. The connected
FD suite covers owning adoption, invalid descriptor cleanup, close-on-exec,
revoke/death EOF and rotated keys. These use the ordinary Linux video ring.

## Android STREAM lease spike

The signature-protected Binder spike grants distinct C/A STREAM pairs separately
from its native AHB SEQPACKET grant. Each active generation requires a fresh owner
token; active-token reuse is rejected, replacement retires both streams, and a
stale owner's revoke cannot close the new grant. Workers solely close server FDs;
setup-failure cleanup shuts down the grant and closes only unstarted endpoints.
Native role-byte exchange has five-second socket limits. It is **not GDP or the
production authentication protocol**. The qualification script uses per-case log
markers without clearing global logcat and touches only its test packages.

## Remaining production integration

A real Android adapter must provision a fresh secret/lease and bind the producer
and receiver providers in signed APK JNI, then bridge FrameLink native video
channel ownership across app UIDs. The current video path still uses private
pathname lookup; adopting control/audio FDs alone cannot bypass that restriction.
Combine those seams into one actual cross-UID A/V session with shared epoch/PTS,
finite completion, generation isolation, wrong-signer rejection, revoke and
producer/consumer/broker death/restart tests. No SELinux/filesystem permission
relaxation, cross-app A/V parity, physical output timing, or M5 completion is
claimed by this adapter increment. M5a remains outside scope.
