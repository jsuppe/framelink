# M5b.4 incremental public audio source

Locally validated, not yet milestone-merged. `framelinkaudiosrc` supports the
owning authenticated context and standalone protected audio-only discovery. It
uses cancellable incremental reads, bounded GDP payload declarations before
accumulation, exact negotiated caps and preserved PCM/timestamps. No socket
chunk-fill wait, timestamp reset, physical device or shared-memory PCM is added.

Linux6/6 and Windows6/6 affected checks passed; Android5/5 executables passed.
arm64/x86_64 native and JNI builds passed. Additional clock-synchronized sink
checks passed Linux/Windows/Android after the six-test suites (test-only change).
Four S16/F32 mono/stereo cases verify exact PCM/37ms PTS origin/10ms durations.
Standalone both startup orders deliver each192-byte PCM packet before another
packet/EOS is sent. A downstream sync=true appsink does not schedule before its
retained PTS (1ms measurement allowance); this is not acoustic-output proof.

Missing-peer timeout without media, immediate cancellation, authenticated idle,
partial-header peer death, hostile2GB payload declaration and mismatched U8 caps
all reject with bounded teardown. Declared payload sizes are checked before the
GDP parser can buffer them; the parser still checks CRC/metadata. A terminal
record must match generation and observed block/byte totals; EOF alone is error.

The first end-to-end receiver-EOS check failed despite all PCM arriving. Tracing
showed gdppay forwards ordinary EOS locally without serialization. A custom
terminal record was sent but its event type was truncated by GDP's16-bit event
field; matching the original32-bit custom type failed. A standard tag alternative
was rejected because gdppay queues changed streamheaders until a later buffer.
The final private custom record is recognized by exact16-bit wire type and
name/version/schema, then converted to standard downstream EOS. This adds no
fake PCM or extra buffering. General custom-event preservation is NOT claimed.
Existing reference receiver and sink regressions pass. Old GDP writers lacking
terminal accounting cannot claim clean EOS with this public source.

Live latency is zero mandatory buffering with unknown maximum, not a promise of
transport/speaker latency. Common A/V mapping/segments/GAP/flush and coordinated
lifecycle remain tasks5/6; examples/bins/full cross-process qualification follow.
