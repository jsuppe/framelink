# Current-profile Android 1080p60 sustained gate

Runtime b5236a4; TCL8483A arm64. 1800seconds, RGBA1920x1080@60, native consumer, pool2/unread1/output1, latency0, transport-only, no-output-preroll, dual-track producer/consumer raw capture, tracing off. Frozen arrival diagnostic17ms.

108000/108000video,86400000PCM sample-frames,180000continuous10msblocks, EOS and complete queue accounting. Zero arrival misses on both tracks. All-frame video arrival p957.66/max10.81ms; output p958.20/max11.49ms. Audio arrival max5.55ms, output max5.97ms. Both tracks retained identical constant PTS mapping (delta0ns). CPU producer55.52%,consumer14.49%; resource slopes effectivelyzero.

Raw files retained in persistent m6-soak/android-1080p60-current archive; SHA256 recorded. Mapping is inferred, not allocation-identity proof. Producer still copies into ring; native consumer does not GPU-sample. No physical output or end-to-end zero-copy claim. This passing run does not independently explain the older60second Android case with two late arrivals.
