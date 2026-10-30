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
  traffic, restart the endpoint, and require a fresh (session ID, generation) identity plus new verified
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

## Linux qualification (2026-10-30)

The full native 33-test CTest suite passed on Linux. The expanded two-process
acceptance driver passed all ten scenarios on the RTX 3090 host using real
FrameLink native transport and GStreamer GDP PCM. Each normal session verified
50 video frames and 50 audio blocks (160x90 at 50 fps; 48 kHz stereo S16LE).
Recovery scenarios kill the original process only after the consumer reports
verified media, retain the other process, then verify two complete replacement
sessions. Each replacement has a distinct random session ID; generation is 1
within each fresh session (not a monotonically increasing cross-session ID).

The driver checks matching sender/receiver session identities, exact verified
media counts, negotiated format, explicit audio fault reason and absence of
false successful completion after faults. It tests both startup orders for BGRA
and NV12, producer and consumer crashes, BGRA-to-NV12 replacement, one omitted
audio block, reset audio PTS, and producer death without recovery enabled.

Run on a native FrameLink-capable target:

```sh
python3 test/m3_apps.py --producer BUILD/framelink_av_producer \
  --consumer BUILD/framelink_av_consumer --plugin BUILD/libgstframelink.so \
  --output /tmp/framelink-m3-evidence
```

Every case retains each peer's command and complete output plus structured
results. Failed cases retain process logs even when no success result is emitted.
Default deadline is 20 seconds per process; the external harness also imposes a
finite deadline and kills only its own children. This is headless transport and
verification evidence, not hardware presentation/audio-output qualification.
