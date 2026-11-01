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
channel ownership across app UIDs. The native backend's original rendezvous uses an abstract Unix socket name
(not a filesystem pathname); Android app isolation prevents treating that name
as an authorized cross-UID rendezvous. The new native connected-FD seam avoids
lookup entirely, while control/audio STREAM adoption remains separate.
Combine those seams into one actual cross-UID A/V session with shared epoch/PTS,
finite completion, generation isolation, wrong-signer rejection, revoke and
producer/consumer/broker death/restart tests. No SELinux/filesystem permission
relaxation, cross-app A/V parity, physical output timing, or M5 completion is
claimed by this adapter increment. M5a remains outside scope.

## Native video connected-FD increment

Android `framelink_ahb.h` now exposes `flCreateChannelFromSocketFd` and
`flOpenProducerFromSocketFd`. Both consume the descriptor on every path, require
connected Unix SEQPACKET sockets, set CLOEXEC/blocking and finite per-I/O bounds,
and retain the existing FrameLink ring/ready/release protocol. The consumer
allocates and handshakes asynchronously; the producer imports native AHB handles.
These are one-lease channels, not automatically reconnecting named channels.

The consumer retains a stable duplicate exclusively to interrupt its handshake
and pump during close, without a descriptor-reuse race. A trusted broker must
retain separate duplicates and shutdown them on lease death/revoke. UID/signature
policy is the broker's responsibility; socketpair SO_PEERCRED is not delegated
caller authentication. Native ring metadata is checked against imported AHBs.
Acquire/release/consumed-fence rules remain mandatory; no added playback queue or
copy is introduced. CPU NV12 normalization/BGRA swizzle is unchanged.

The signed APK SessionProvider/native-sink binding and real cross-UID A/V
qualification are still the next task. The new native test verifies same-UID
separate processes, not permission delegation or physical GPU/display timing.

## GStreamer native channel adoption

`SessionProvider::openVideo(config, producerRole, timeoutMs, error)` optionally
returns an owning native channel matching the authenticated selection. The A/V
host checks dimensions/format and transfers it through the elements'
`adopt-channel` action signal before leaving NULL state. Signal signature:
`gboolean adopt-channel(flChannel*)`; TRUE transfers ownership, FALSE leaves it
with the caller. Callers must supply the correct producer/consumer role and
serialize setup/state changes. This is a native application API, not a numeric
handle intended for gst-launch strings. Finalization closes a channel adopted
before a failed start. A stopped/revoked lease never falls back to named lookup;
reuse needs a newly adopted channel. Existing no-provider behavior is unchanged.

This changes rendezvous/ownership only. System-memory caps still copy through
FrameLink's CPU mapping; native Gst buffer caps/zero-copy policy remain M5a.
ProducerMapped startup still imports video before PRIMED/GO and preserves media
PTS. Native renderer externalVideo ownership stays separate and is rejected if
combined with this element-owned provider to avoid two owners of one ring.

Fresh Linux focused tests passed3/3 (17.75s): timestamp contract, ordinary slot
preconnect, broker producer. The latter now has14 real-peer cases including
adopted BGRA/NV12 (all20frames delivered) and explicit native lease refusal.
Android arm64 and x86_64 full A/V native builds passed. Signed APK wiring and
cross-UID A/V runtime qualification follow; these are not claimed by this seam.
