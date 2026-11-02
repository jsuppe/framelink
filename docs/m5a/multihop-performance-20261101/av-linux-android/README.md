# Simultaneous paced native video + GDP PCM qualification

Task13, local evidence; not milestone closure or physical-output proof.

Linux RTX3090 and Android TCL8483A arm64 each ran 3720 frames of RGBA1080p60,
two original allocations, independently paced audio/video, one/two process boundaries.
Linux one/two:59.995783/59.995725fps; Android one/two-final:59.999884/60.000190fps.
Every accepted run retained 2976000 stereo PCM sample-frames (11904000 bytes),
exact video/audio identities, samples and TIME-segment running times. The deliberate
producer A/V media offset was37ms and did not change. Arrival and transport-delay
skew are reported separately; this is not synchronized playback/physical lip sync.
Traces account for3720/7440 boundary joins and3720 GPU completions, zero loss.

PCM uses test-only loopback GStreamer GDP/TCP, not an authenticated production
session or a new zero-copy audio API. GPU consumer samples shared originals into
private output and reads36 verification bytes/frame. Source CPU generation remains
explicit; no relay image map/copy. Android is a same-UID process harness, not a new
signed cross-UID qualification. Existing task12 covers the separate signed path.

## Retained failures / measurement correction

Android first two-hop media passed, but its relay recorder exhausted65536 entries:
32501 dropped events. That trace fails acceptance and is retained as av-two-full.
Repeat av-two-full-large changes only FRAMELINK_TRACE_EVENTS=1048576; it passes.
The one-hop recorder had no loss at default capacity.

The analyzer originally compared consumer arrival against producer push-return time.
A receiver can run before enqueue returns. Fixed to retain the pre-enqueue source
marker causal bound, and relay-observed arrival before downstream arrival. Regression
controls accept legitimate post-enqueue ordering and reject pre-source arrival,
changed PTS and missing PCM. No media/frame acceptance criterion was relaxed.

## Reproduce

Set FL_BENCH_AUDIO=1 FL_BENCH_GPU=1 FL_BENCH_HOPS=1 or2, FL_BENCH_OUT=prefix,
FRAMELINK_TRACE=traceprefix, FRAMELINK_TRACE_EVENTS=1048576. Default3720frames,
120warmup,1080p60,pool2. Run fl_native_boundary_benchmark; collect each process CSV.
Use tools/check_boundary_av.py prefix --hops N;
tools/check_boundary_benchmark.py prefix1 prefix2;
tools/check_boundary_trace.py --frames3720 --hops N --gpu traceprefix.*.csv
(with normal spaces between flags/values). Binary SHA256 files identify measured
builds. Android x86_64 built only; arm64 ran. Source comment correction after builds
has no executable effect. Standalone video JSON pcm_qualified=false refers to the
video checker; separate audio analyses above establish PCM transport validation.

Windows simultaneous A/V, resource/flush accounting and final task13 review remain
open. Native transport performance is not decoder/display/physical-output proof.
