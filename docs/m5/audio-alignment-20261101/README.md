# Android forward-audio alignment repair

## Mechanism and scope

The previous OpenSLES ProducerMapped stall was narrowed to the output sink,
not FrameLink/GDP payload corruption. A failed buffered capture recorded an
initial device/system calibration ratio of about1.625 followed by stalled PCM
admission while the device clock continued advancing (see the preceding
coordinated-audio evidence). A subsequent sink trace exposed **negative output
sample counts in a forward stream**, even though that run completed its input
counters. Thus completion/PCM-before-resampling counters alone were insufficient.

GStreamer1.24 GstAudioBaseSink RESAMPLE aligns render_start with next_sample but
not render_stop. A large calibration phase correction can move the aligned start
past the stop. The default1-second discontinuity grace allows that adjustment;
the signed negative span requests reverse resampling and can progressively
expand later writes and backpressure admission. The recording-ringbuffer test
executes the real GstAudioBaseSink implementation, not a duplicate algorithm:

- 40ms phase change,1600 input samples: unguarded output-320; guarded1600.
- Same change with200/1-sample blocks: unguarded-1720/-1919; guarded200/1.
- Small20us correction: both unguarded/guarded output1599 from1600 samples.

The OpenSLES-specific guard uses public sink properties: immediate discontinuity
handling and alignment threshold <= half the negotiated PCM block. It adapts to
block size and caps, retains normal small-drift RESAMPLE behavior, and does not
rewrite timestamps/durations/payloads, widen queues, or relax any deadline.
Other backends keep their existing policy. This is an output phase-discontinuity
repair, not a claim that resampled hardware PCM is bit-identical to input.

## Device evidence

TCL8483A/API35, separate signed producer/client UIDs, GStreamer Android1.24.13:

- Full20-case signed-app matrix passed, including BGRA/NV12 identity/PTS,
  original PCM/PTS, authorization/rejection, replace/revoke, independent peer
  death/restart, background/reopen, actual audio-focus loss/reopen, and audible
  BGRA plus audible NV12.
- Untraced BGRA10s:300/300 video/posts and480000 original PCM samples/channel.
- Untraced NV12 15s:450/450 video/posts and720000 original PCM samples/channel.
- Buffered raw-file trace:60/60 sink render and complete write calls,96000 PCM
  input samples, zero non-forward spans, zero recorder loss; one actual phase
  resync. Both video and original PCM identities/PTS also passed.
- The trace checker rejects mutated negative spans, missing records, short
  writes and reset video timestamps. Normal stdout/logcat is not an exhaustive
  trace transport: the private cache artifact is written after playback stops.

Native arm64 APK and x86_64 Release JNI builds passed. Linux real-sink regression
and applicable core checks are retained separately. Windows/Linux hosted CI on
this repair is a subsequent gate; older CI success does not cover new source.

## Remaining limits

This establishes functional30fps160x90 cross-app delivery and the diagnosed
forward-resampling failure's correction on this device. It is not Android4K/
1080p60 throughput, physical scanout, acoustic synchronization, or all-driver
qualification. Accepted Surface posts and Gst ringbuffer writes are API-level
evidence, not optical/acoustic measurements. Device resynchronization may alter
output phase/sample placement; transport source PTS remain untouched.

The explicit audio-master diagnostic policy is still unqualified on this
Android device: its clock starts late and a synchronous Surface callback can
backpressure the video ring. ProducerMapped remains the example default.
The coordinated-start API is portable and independently tested, not a blanket
claim that the Android audio-master diagnostic is lossless. No silent fallback.
