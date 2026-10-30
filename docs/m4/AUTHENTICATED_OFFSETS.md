# M4 authenticated application timing gate

Independent test `test/m4_app_offsets.py` exercises the real separate producer
and streaming consumer executables with authenticated rendezvous and GDP audio.
Runtime implementation: `a007e56` + `b594dfe` (the tested binary predates only the
help-text addition in b594dfe, not its runtime). Linux build
`/tmp/framelink-m4-playback-build`, 2026-10-30.

Three cases passed:

- +37,000,000 ns source audio offset: received audio origin 37,000,000 ns;
  audio end 1,637,000,000 ns, video end 1,600,000,000 ns.
- +123,456,789 ns (not frame-aligned): origin preserved exactly;
  audio end minus video end remains 123,456,789 ns.
- Source audio PTS reset with the initial 37 ms offset: consumer detects
  discontinuity, both peers exit nonzero, no successful playback/timing-complete
  event is emitted. Generic timeout/failure alone does not pass this control;
  the test requires the discontinuity diagnostic.

Both successful cases verify all 80 audio blocks / 76,800 samples. Production
receiver validation checks their original timeline continuity; no consumer
per-track origin rewrite is performed. Video startup origin is observed, not
assumed, and must be a real frame-aligned PTS within the declared video range.
The receiver observed 23/80 and 28/80 video frames in these runs: **this is not
lossless frame-delivery or throughput qualification**. These tests establish
source timing preservation for received media and the independent audio drain
endpoint, not optical/acoustic playback alignment. The pipeline uses a fake
scheduled audio sink and explicitly reports `audio_master=false`.

Producer fixture supports nonnegative audio offsets only. Negative relative
offsets require a common positive video origin/media-range protocol and remain
covered at raw transport level, not by this application fixture.

Raw producer/consumer logs and result JSON are in `app-offset-evidence/`.
The CTest registration `m4_authenticated_offsets` is a hardware integration test,
not a hosted CPU-only whitelist addition. It requires the above runtime commits.
