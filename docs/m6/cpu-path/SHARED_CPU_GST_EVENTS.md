# Authenticated event ordering for CPU video

Explicit shared-CPU source/sink can now use the existing separately authenticated
video event stream. They still reject audio tracks pending A/V qualification;
this does not enable bin discovery or change any default/native path.

The sink's existing control sender publishes GAP, discontinuity barriers and END.
CPU source joins those controls with ordered CPU media, requiring every event's
`afterFrames` to equal its exact delivered-frame count. First frame and every
metadata discontinuity require the exact barrier PTS. Unlike native latest-wins
handling, no old barrier is silently skipped: CPU mode does not drop frames.
GAPs retain source duration and use the common receiver mapping; they are emitted
at the source pad before the associated output frame, or before terminal EOS for
trailing gaps. Pending GAPs remain bounded at32. END must match exact count and
last source PTS (zero for empty), independently of the CPU socket EOS marker.
Contradictions fail instead of advancing the stream or reporting successful EOS.

`flSharedCpuWaitReadable` adds a non-consuming, externally serialized0..60000ms
readiness wait. Timeout leaves the channel live and does not infer slot release;
EOF/error readiness is resolved by the actual receive call. EINTR retries retain
the original finite deadline. CPU source polls readiness in at-most50ms slices,
clamped to the remaining session idle budget, checking cancellation/generation
and event-channel failure between waits. A FLUSH or failed authenticated control
stream therefore interrupts an idle CPU source without waiting out its60-second
socket timeout. Same-generation flush/resume is not supported: a fresh session
and CPU channel are required. No queue-fill wait or polling delay is added when
media is already readable.

## Validation

Linux27/27 selected CPU, contract and GStreamer regressions pass. Android15
runtime binaries pass and both Android ABI builds pass. After tightening the
last polling slice to the remaining idle deadline, all three CPU event/source/
roundtrip tests were rebuilt and rerun on Linux/Android, with both ABI builds.
Broad results precede only that localized correction; final hashes/results are
under `shared-cpu-gst-events/final/`.

New tests authenticate the event stream using a shared credential, preserve GAP
PTS/duration through the receiver mapping, verify barrier-before-frame and
trailing-GAP-before-EOS, and reject wrong barrier count/PTS, wrong GAP count and
wrong END count/PTS. An idle60-second channel is cancelled by an authenticated
FLUSH within the frozen1-second gate. Direct readiness timeout/invalid-timeout
checks leave queryable channel state unchanged; readiness subsequently observes
a valid release. The actual CPU sink/source roundtrip now also authenticates its
event stream, validating the real sink's first/discontinuity barriers and END.

These are functional video controls, not A/V, seek/pause-resume, sustained target
rates or final M6 acceptance. Next: A/V terminal coordination/bin selection and
broker/rendezvous provisioning; Windows CPU allocation remains outstanding.
