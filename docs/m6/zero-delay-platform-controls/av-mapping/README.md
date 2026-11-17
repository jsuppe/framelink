# Producer-to-consumer A/V timestamp mapping controls

The benchmark now records source-pad PTS/push lateness for both tracks. Video
raw filenames are unchanged; audio producer records use `.audio-push.csv`.
Both vectors are prefaulted before media to avoid instrument growth masquerading
as transport growth. This is benchmark instrumentation, not production tracing.

Linux BGRA, Windows BGRA and Android RGBA1080p60 controls each completed300/300
frames and240000 PCM sample-frames,500 contiguous audio blocks, with zero17ms
arrival misses. All used pool2/unread1/output1, latency0, sync=false and
explicit `--no-output-preroll`. For every recorded video frame and audio block,
the inferred producer-to-consumer PTS mapping was constant and the difference
between those two track mappings was exactly0ns.

Seven analyzer tests include a positive37ms producer A/V offset preserved on
both sides, and a negative audio-only37ms rebase that must reject. Add
`--producer-audio PRODUCER.audio-push.csv` to the raw analyzer together with
`--audio CONSUMER.audio.csv --expected-audio-blocks N`.

This verifies relative timestamp preservation for these captures, not physical
output, GPU sampling, arbitrary segment/rate changes, or independent allocation
identity. The inferred mapping cannot independently detect a common whole-stream
shift. The earlier30-minute capture lacks producer audio records and must not
be retrospectively promoted to this stronger dual-track check.

Linux114/114 passed at8541985 before this benchmark-only instrumentation;
its affected benchmark smoke passed again after the change. Short cross-platform
checks are not substitutes for final integrated sustained qualification.
