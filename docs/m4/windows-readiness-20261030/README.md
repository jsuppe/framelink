# Windows follow-up: segment, import failures and endpoint publication

Sources: FrameLink1f38ffc, renderer8602fa5. Same isolated Caspar Windows trees,
RTX5090, MSVC19.44/GStreamer1.28. No live demonstration replaced.

- `av_audio_segment`, `m4_av_recovery`, `m4_authenticated_offsets`:3/3 passed.
- BGRA and NV12 renderer runs each injected40 failed frame adoptions. Both passed;
  each reported exactly40 failures, continued scheduling and exited normally.
  Due decisions248/247 respectively. Queue<=6; physical playback/lossless delivery
  not claimed. Release build, so no validation-layer delayed-GPU qualification.

## Confirmed endpoint publication race

Windows `flCreateChannel` spawned `acceptLoop` and returned before that thread
created the named pipe. `flOpenProducer` correctly fails fast for nonexistent
names. An immediate producer could therefore see NOT_FOUND after successful
create; the GStreamer sink drops that first buffer and awaits future input.
This explains a mechanism for single-frame timestamp tests to time out, and
why extra logging could mask the behavior. Original failing executions were not
instrumented enough to prove every missing sample had this cause.

Regression `windows_endpoint_ready` pins the test to one allowed CPU and increases
caller thread priority, exercising valid delayed accept-thread scheduling without
sleep/retry. Against the unpatched library it deterministically failed cycle0
with FL_NOT_FOUND. Fix56a9596 waits for one-shot promise notification after named
pipe creation, before returning success. Creation failure reports FL_INTERNAL.
No per-frame waits, logging, public API or producer retry behavior changed.

Patched endpoint regression5/5 passed (12 immediate create/open cycles each).
Original timestamp-contract repeated and full-suite evidence follow separately.

Post-fix original timestamp test100/100 passed without instrumentation. Initial
post-fix full49-test run passed48/49: existing av_offer_bounds fixture had not
sent a segment before PCM and therefore correctly failed the new mandatory
TIME-segment contract. Test-only fix2918d4d supplies a real SEGMENT probe event;
affected offer-bounds and segment tests2/2 passed. The failed48/49 log is retained.

Final complete Windows CTest after both fixes: **49/49 passed**, 110.87 seconds.
Exact tested FrameLink source:1f38ffc plus56a9596 and2918d4d. The unmodified
`gst_timestamp_contract` passed100/100 quiet repetitions before this final suite.
The new deterministic startup test is also included in the49-test suite.
Renderer8602fa5 rebuilt against the readiness-fixed FrameLink: both final BGRA/NV12
40-failed-adoption injections passed again. Raw evidence in `native-final/`.
