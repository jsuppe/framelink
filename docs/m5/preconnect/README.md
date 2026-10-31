# Producer resource readiness before the media epoch

## Causal order

In coordinated mode 2, the consumer completes source/channel setup before READY.
The native renderer's onSession callback waits for its render-thread channel
creation. Producer opens its audio connection, receives READY, starts its
pipeline, primes PCM, awaits PRIMED, chooses one final shared epoch, awaits GO,
and releases the gated media branches.

Previously framelinksink deferred flOpenProducer until sinkRender, after this
clock was already running. Windows traces show approximately 158 ms device/resource
import there. The first frame therefore misses its deadline and the following
scheduled buffers burst into a finite ring. Increasing acquire-buffer timeout
would not fix the separate connection setup.

## Change

`framelinksink connect-timeout-ms=0` retains default lazy/reconnecting behavior.
A positive value opts into producer connection in GstBaseSink::start. Coordinated
sample sessions set it to their existing session timeout. Thus import completes
before the pipeline-start call returns, before PRIMED/final epoch/GO and before
any media render. Original PTS, A/V offsets, configured 100 ms playback latency,
and wire protocol remain unchanged. Initial geometry is queried from the
negotiated consumer ring; normal set_caps validates/requests geometry afterward.

Retry waiting is deadline-bounded and fails startup rather than silently losing
first frames. The deadline is checked after import too; an uninterruptible driver
call cannot be forcibly stopped by this property. Existing process watchdogs
remain necessary for driver hangs. Consumer absence is a bounded error only in
opt-in mode; old gst-launch lazy/drop semantics remain the default.

## Deterministic validation

`python3 test/m5_preconnect.py --build BUILD --output EVIDENCE`

The fixture inserts 350 ms before producer import via
`FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS`. A test-only
`FRAMELINK_TEST_LAZY_PRODUCER_OPEN=1` retains the old first-frame connection and
must lose or late-drop frames. Trace operations gst.producer_preconnect and
gst.producer_open prove that connection finishes before first gst.frame_receive
in the fixed path, and occurs after that event in the old path.

Both decoded BGRA/NV12 use 100 frames at 50 fps, exact media hashes/PTS/audio-duration
checks. Fixed paths receive and schedule 100/100 with latency 100 ms unchanged;
negative controls lose frames and/or mark them late. Separate synthetic sessions
exercise producer-first/consumer-first order. A missing consumer fails the 100 ms
readiness deadline. Existing m5_startup covers peer cancellation and restart.

Not physical presentation qualification; final native Windows 45-second matrix
belongs to the exclusive Windows validation owner.

## Slot mode and repeated starts

The shared `flOpenProducerSlot` helper first claims an existing exact channel,
then requests geometry **only when both width and height are nonzero**. Thus
pre-caps slot selection is geometry-independent on every backend; BGRA default
format is not sent as a geometry request. The opt-in start clears cached video
caps so a prior stream cannot resize the next consumer ring using stale geometry.
Default lazy mode is unchanged.

`gst_preconnect_slots` reuses the same sink on two NV12 slot rings, first 64x48,
then 96x64, with fresh sources and no initial caps. It verifies each claimed ring
retains its original geometry/format, rejects a duplicate producer, then receives
a frame after new caps. Removing the caps reset makes the second initial geometry
assertion fail; the fixed regression passes. This is Linux runtime evidence plus
shared-helper source inspection, not a claim of Android/Windows execution.
