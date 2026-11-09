# Windows startup isolation — diagnostic, not acceptance

Base revision `548fe19b8c11b84758653aafe0df812f92cbc9ab`, <win-host> Windows.
Original integrated failure remains in `../windows-integrated-548fe19`.
No production changes, threshold edits or clean-repeat waiver were made here.

Existing `test/m5_external_pipeline.py` was run sequentially with native and
GStreamer duration traces, once each for `audio` and `audio-coordinated`, then
with the existing `FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS=1200` setup fault.
`run.py`, `delayed.py`, status and all raw output are included.

* Uninjected legacy audio: both formats 100 source / 95 received and scheduled
  video, 100 PCM blocks / 96000 samples, zero late video. Producer open occupies
  the first render (155–158 ms); this did not reproduce the original 1529 ms.
* Uninjected coordinated: both formats 100/100 video, same exact PCM, zero late.
  Producer preconnect costs 153–162 ms before the final shared playback epoch.
* Injected legacy: both processes exit 1 in BGRA, only 9 source-video traces;
  consumer reports **audio playout budget exhausted**, late video, then the
  producer encounters disconnected audio I/O. NV12 is not reached. This is NOT
  the same terminal assertion as the original integrated failure.
* Injected coordinated: both formats 100/100 video, 100 PCM blocks / 96000
  samples, zero late video, exit 0. Resource opening is outside the media epoch.

Causal conclusion: legacy uncoordinated opening exposes first-frame blocking;
existing coordinated startup isolates this injected setup delay. These controls
neither attribute the original unusually long open nor prove generic decoder
readiness. No default clock policy was changed. The original timing acceptance
is **suspended**, owner Evelyn; resume with original-scale delay attribution
(native device/import versus OS scheduling) and an explicit compatible remedy.
Continue Windows producer-bin/native-session integration while retaining this
gate. D2/D3 and cross-platform acceptance remain open; no M7/API freeze.
