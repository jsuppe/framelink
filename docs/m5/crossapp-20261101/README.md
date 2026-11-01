# Signed Android cross-app A/V qualification — 2026-11-01

**M5 is not closed.** Parent executed this work directly and sequentially after
confirming the Oct31 worker-to-parent execution gap. No external input blocked it.

## Implemented

`android/crossapp` binds real connected-FD FrameLink video and authenticated
control/GDP PCM to a signature-authorized broker, separate producer process,
and different-UID consumer. Fresh leases, secret/UUID, stable revocation handles,
Binder death, replacement and original media verification are implemented.
Gst system-memory mapping and CPU Surface conversion remain explicit copies.
No SELinux changes, shared UID or public named rendezvous are used.

The consumer checks every BGRA/NV12 pixel, sequential frame identity, original
video PTS, every stereo PCM sample (sine quantization tolerance1LSB), and original
first-sample PTS. Every successful run requires exact frame/sample totals and
one successful Surface post per frame. These are accepted posts, not scanout.

Device: TCL8483A, Android15/API35, SELinux Enforcing, arm64. Broker/producer
UID10304; consumerUID10305; producer and broker are separate processes.

## Accepted functional evidence

- First signed cross-UID BGRA run:60/60 frames/posts and96,000 PCM samples/channel.
- Full replay `fl-crossapp-matrix-repeat-20261101`:15 cases passed, including
  BGRA/NV12, wrong signer/key, revoke, replacement, independent producer/broker/
  consumer death and restart, background/reopen, one actual OpenSLES completion.
- `fl-crossapp-focus-20261101`:2 cases passed. A competing real AudioManager
  focus request triggered cancellation of both tracks; fresh reopening completed.
- `fl-crossapp-invalid-20261101`:2 cases passed. Unsupported sample format and
  invalid bounds rejected before endpoint grant.
- Linux matching Release build and affected CTests3/3 passed,17.69s:
  `av_audio_segment`, `gst_preconnect_slots`, `broker_producer_stream` (14 peer
  scenarios). New observer negative control rejects exceptions without advancing
  sample accounting or rewriting PTS.
- arm64 debug APK and x86_64 Release JNI builds pass. x86 build is not VM runtime.
- Test apps are force-stopped after each campaign; no default audio device,
  volume, global display mode or system permission policy was changed.

The consumer-death harness initially required the literal Binder-death reason.
Android instead destroyed the bound-only broker when its last client died,
retiring endpoints as `broker-destroy` first. Producer failed safely. The harness
now accepts either causally valid retirement path. Original failed assertion is
preserved in `fl-crossapp-matrix-20261101`; it is not counted as a software pass.

## Open issue: intermittent real OpenSLES admission stall

Three actual audio runs failed despite the same silent transport succeeding:

- `fl-crossapp-matrix-tail-20261101/audible.log`:57/60 video;30,400/96,000 PCM.
- `fl-crossapp-audio-off-0/audible.log`:60/60 video;36,800/96,000 PCM.
- `fl-crossapp-audio-metrics-repeat-3/audible.log`:60/60 video;40,000/96,000 PCM.

The third capture shows the audio queue filling around0.7 seconds, followed by
increasing admission drift and incomplete media. Existing deadline/completion
guards rejected these sessions. No guard, timestamp, buffering limit or latency
was relaxed. Detailed logging initially coincided with successful runs; reduced
metrics reproduced a failure, so logging was not treated as a fix.

Later failure-only-instrumented runs, all three10-second idle-separated runs
(300/300 frames/posts and480,000 PCM each), and the complete replay passed.
Across the retained diagnostic/control campaigns there are39 successful and3
failed audible observations. They are selected short diagnostic controls, **not
a random failure-rate estimate, an endurance gate, or evidence of a fix**.
No root cause has been established. Failure-only clock calibration and opt-in
bounded/buffered diagnostics are now available for the next failing observation.

Follow-up controls: six untraced150-frame sessions with15/37/63/91/125/173ms
injected pre-PRIMED delay all passed; the phase control did not reproduce the
stall. An explicit legacy AudioPreferred control failed strict video identity
verification after5 frames (17,600 PCM samples). It reported the audio device
clock, but this protocol lacks the ProducerMapped PRIMED/GO startup barrier.
That result is not a fix or a qualified lossless fallback. The example default
remains ProducerMapped; both policies are explicit, with unchanged core timing
and deadline semantics. These follow-up records live in `controls/` and are
additional to the earlier39/3 audible observation count above.

**Real ProducerMapped OpenSLES playback remains conditionally demonstrated, not
reliably qualified.** The earlier AudioPreferred same-APK evidence remains a
separate mode. M5 closure still needs this issue resolved or a concrete, explicit
supported-mode decision with corresponding qualification—not silent fallback.

## Limits

This source verifies transport/common-timeline preservation at160x90/30fps; it
does not replace Windows1080p60 sustained results or claim Android high-resolution
performance. Native caps/zero-copy Gst integration remains M5a. Real route unplug,
system suspend, physical display/speaker alignment and x86 VM runtime remain
unqualified. Foreground-only cancellation/reopen is supported; background media
service playback and arbitrary separately signed client grants are not.

Build/run instructions: [`android/crossapp/README.md`](../../../android/crossapp/README.md).
Raw files preserve initial failures and the subsequent exact positive/recovery
results rather than overwriting earlier evidence.
