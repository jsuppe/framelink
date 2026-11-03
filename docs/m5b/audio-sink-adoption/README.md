# M5b.3 authenticated-context audio sink (partial)

`framelinkaudiosink` is registered. It accepts an already negotiated typed session
context plus its authenticated, single-claim producer audio stream. It wraps
capsfilter/queue/GDP/GIO, preserves PCM bytes and timing, checks sample-frame
alignment/continuity/duration and bounded block size, and reports copied PCM.
No rendering or playback pacing is added. A coordinator cancellation/replacement
revokes its owned audio connection; a second claim/wrong role is rejected.

Linux4/4 and Windows4/4 affected tests passed: gst_audio_sink, gst_session_context,
gst_session_transport, existing video gst_sink_stats. Android audio sink executable
passed; arm64/x86_64 builds passed. Four nominal S16/F32 mono/stereo cases retain
exact bytes,37ms PTS origin,10ms durations. Missing authentication, actual boundary
missing PTS and oversized blocks reject. Tests do not claim throughput/physical output.

GDP's outer byte segment is adapted for non-seekable GIO output, retaining its
encapsulated PCM segment/PTS. The reference receiver uses giostreamsrc blocksize1
ONLY in this small content test: default full-chunk reads delayed initial packets.
The public source must use incremental socket reads, not this byte-at-a-time path.
GstBaseSrc supplies first-buffer PTS0, so invalid-PTS injection is after BaseSrc.
Initial failing fixtures and discovery led to these corrections, not relaxed checks.

Task3 is still OPEN: standalone local discovery/configuration, additional failure
and backpressure handling, final public properties/documentation remain. Context
adoption is not the complete gst-launch user flow. Public source/bins are not yet
implemented. Audio remains serialized/copied, not shared PCM.
