# M3 focused runtime review (2026-10-30)

Reviewed `samples/av_apps.cpp`, `av_auth.cpp`, and `av_rendezvous.cpp` against
integration source `9f40b78`, plus the independent runtime failure-path fix
`9d1eed6`.

## Findings fixed

- Passing `err.c_str()` beside a function that modifies `err` in the same call
  can leave a stale pointer due to unspecified argument evaluation order. The
  runtime now passes a reference to the string object and copies it only after
  the operation completes.
- Audio timestamp rejection previously leaked the generated comparison buffer.
  It is now RAII-owned; partial map failure also unmaps the received buffer.

The full ten-case native Linux process matrix passed again after these fixes,
including the timestamp rejection and crash-recovery paths. A publicly writable
rendezvous directory was rejected with a complete, correct diagnostic.

## Checks

- Fixed-size offers validate counts/version and require zero unused fields;
  session helper validation rejects invalid or unoffered selections before media.
- Both roles authenticate before capability or GDP parsing. The audio endpoint
  key is derived from the private root key, session identity and generation.
- Tokens are neither sent nor logged. Authentication uses role/direction-tagged
  HMAC challenge responses with fresh nonces and fixed-length digest comparison.
- Owner-only directory/file checks, native lifetime locks and token-matching
  cleanup prevent accidental removal of replacement-owner records. Lock files
  remain in place to avoid split ownership. Stale records are safe to replace
  after a crashed owner releases its OS lock.
- Network I/O has 1–2 second socket timeouts. Blocking accept, repeated missing
  record reads, startup, teardown and reconnect attempts additionally have the
  process-wide watchdog (CLI maximum 60 seconds). Connect-refused retries do not
  consume media-attempt budget but remain under that same watchdog.
- Pipeline/socket/reader lifetimes were reviewed: reader cancellation and socket
  closure unblock I/O, pipeline shutdown unblocks queued pushes, then its worker
  joins before owning objects are destroyed.
- Optional playback uses bounded tee queues. Verification still runs separately
  on both tracks. Device startup/teardown hangs remain bounded by the process
  watchdog; actual audio-device/display behavior was not qualified by this review.

## Boundaries

This is a current-user local sample, not a network/untrusted-media service.
A second process running as the same OS user is within its trust boundary.
The sample supports one consumer session at a time, not multiple simultaneous
receivers. The loopback port is rendezvous information, not a secret. A local
unauthenticated client may consume bounded connection-attempt budget; the sample
does not claim denial-of-service resistance.

The watchdog intentionally terminates the process on a hard stall. It cannot
provide graceful teardown in that case; crash recovery is verified separately.
This review is not physical playback, security certification or M4 drift-recovery
qualification.
