# M3 independent media application acceptance

The test driver launches real producer and consumer OS processes. A successful
process exit alone is insufficient: received video pixels, PCM samples and
first-sample timestamps must be checked by the receiver, with counters emitted
in machine-readable final results. Tests use finite deadlines and retain each
process's command, exit status and log. This is application/transport evidence,
not speaker-to-screen synchronization qualification (M4/M5).

## Acceptance cases

- BGRA and NV12: producer-first and consumer-first. Require nonzero verified
  frames and PCM blocks, no pixel/sample/timestamp errors, and agreement on
  session/configuration identity.
- Producer restart: retain consumer, kill the active producer after confirmed
  traffic, restart the endpoint, and require a new generation plus new verified
  media. Old generation must never be accepted after the new handshake.
- Consumer restart: retain producer, kill the active receiver after confirmed
  traffic, restart it, and require verified traffic on the new connection.
- Format renegotiation: restart producer with the other supported pixel format;
  require the consumer to acknowledge/verify the new descriptor, not interpret
  old buffers with the new format.
- Missing audio: deliberately omit audio while video is available. Require an
  explicit audio failure/timeout and bounded termination, not generic success.
- Peer death: kill peer after established traffic. Require detected disconnect
  or deadline, bounded exit, and no stale successful completion.
- Recovery and rejection assertions must refer to received media and identities,
  not only matching log text or exit status.

## Evidence boundaries

Headless verification intentionally uses no physical output device. Optional
playback requires a separate available display/audio device test. Windows runs
use the same Python process driver (no POSIX signals, shell commands or external
timeout utility). Hardware-dependent tests must not be labeled passed on hosted
CPU-only runners. Android native binaries are not Android application/service
lifecycle qualification.
