# Windows three-process decoded native reservation relay

Base `fda55c75329eb40e648715317d20a7ef2ad93424`; status.json records exact source overlays.
The source, checked relay and final consumer run in three independent processes.
The source uses real x264/h264parse/D3D11 H.264 decode of 320x240 NV12 at 30fps,
then an explicit same-device GPU color/scale transform into the two original
64x32 RGBA FrameLink GstMemory reservations. Decoder storage remains retained
until a bounded D3D11 completion query succeeds.

The varying snow fixture delivers 16 distinct decoded pixel digests and 25,600
synthetic stereo PCM frames. A source-side FNV-1a digest of every active RGBA byte
is carried in the first eight bytes of each PCM block; the final consumer computes
its own digest, compares it and checks every remaining PCM byte. All 16 source
digests must differ, preventing constant-image success from concealing stale frames.
Both reversed NT allocation identities, frame count, source-relative PTS,
constant 33,333,333ns decoder duration, absent DTS, stream generation, common 37ms
A/V offset, ordered GAP/barrier/END and authenticated terminal accounting are checked.

The original encoder PTS origin is explicitly subtracted once in the source.
Decoder output has rounded PTS intervals but constant durations; the original
fixture incorrectly expected alternating durations and failed at frame 2.
The negative and its source are preserved in initial-duration-negative; black-control
preserves the corrected-duration run. No production timing was changed.

For each frame, duplicate submission rejects, the final remote reader blocks reuse,
and after explicit remote drain both the original GstBuffer and separately retained
GstMemory prevent reacquisition until final local unref. The other allocation remains
reserved throughout each check. Child processes are checked for exit0 and job-contained.

This is broker-composed integration, not automatic-bin relay, generic foreign decoder
import, a real audio decoder, or target-rate qualification. The relay itself does not
map or copy video. Upstream decode/color/scale and source/final pixel-oracle readbacks
are explicit; this is not raw-decoder zero-copy. No production API, exported symbols,
wire layouts or overload/fallback defaults changed. Source PTS is not physical output.

Loom pre-edit context was attempted and unavailable (missing chromadb). Existing
D2/D3 decisions, Windows startup/lifetime acceptance failures and M6 gates remain
open. Hold before M7. Linux/Android campaigns remain deferred.

Next: exact-revision integrated Windows build/API verification, then bounded
independent-process faults and remaining automatic-bin/producer integration.
