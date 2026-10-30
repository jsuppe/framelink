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

## Remaining integration

A real Android adapter must allocate/delegate separate STREAM pairs, implement
lease ownership/death/generation tracking, run the existing producer protocol
on the other ends, and bridge video channel ownership. No SELinux or filesystem
permission relaxation is involved. The producer still uses the desktop listener
entrypoint; adding a producer-side provider and wiring both ends into signed
APKs is the next step. The receiver remains on its existing video channel;
this seam does not by itself adopt FrameLink video-ring handles.
