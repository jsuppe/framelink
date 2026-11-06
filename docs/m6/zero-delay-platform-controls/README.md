# Zero-added-delay platform controls — 2026-11-06

Settings: native consumer,1080p60, pool2/unread1/output queue1, latency0,
unsynchronized measurement sinks,17ms diagnostic arrival budget,60seconds.
Windows BGRA:3600/3600 frames +2880000 PCM sample-frames; complete queue
accounting; no deadline misses; video arrival p955.71ms.
Android RGBA: same complete counts/accounting, but2video arrivals exceeded17ms,
maximum19.10ms. Instrumented --raw repeat completed all counts with zero misses
(max9.97ms). This does NOT explain or repair the original tail events. Both
results are retained; the next30-minute raw run is intended to capture recurrence.

Benchmark record vectors for queue timing are now preallocated/prefaulted like
the existing arrival/lateness vectors so their growth does not masquerade as
transport leakage. Linux/Windows/arm64 benchmark builds passed. Post-change
Android5second preflight output is included. No production behavior changed.

Zero-copy producer, final playback and physical output are not claimed.17ms is
an explicit diagnostic threshold, not approval of a universal latency contract.


## Raw timing correlation

`tools/m6_raw_timing.py PRODUCER.push.csv CONSUMER.video.csv --expected 3600
--budget-ms 17` validates the expected count and strictly increasing PTS,
then checks a constant inferred producer-to-consumer PTS offset before
comparing push and arrival lateness. It includes startup and drain. Exit 2
rejects an invalid join; exit 1 reports diagnostic arrival misses; exit 0
means the join and chosen arrival diagnostic passed, not full qualification.
Five tests reject truncation, missing consumer records, duplicate PTS and
changed mapping, and retain a known late frame.

The offset is inferred, not independently captured from negotiation: this
cannot exclude a whole-stream constant shift and is not allocation/identity
proof. The residual includes producer copy, transport, consumer work and
mapping error. Correlation is not by itself causal attribution. The saved
60-second Android raw control joins 3,600 records, with no 17 ms misses;
this does not repair the earlier two late arrivals.


## Completion and startup-output controls

The benchmark now requires independently calculated video and PCM counts,
complete paired queue records, and the existing consumer audio-continuity check
before reporting completion. Raw analysis additionally supports
`--audio CONSUMER.audio.csv --expected-audio-blocks N` for this benchmark's
fixed 480-sample/48 kHz audio blocks; six tests cover the correlation checks.

`--no-output-preroll` explicitly sets measurement appsinks `async=false`.
It is independent of `--transport-only` (`sync=false`) and configured latency.
Defaults and production playback sinks are unchanged. All-frame arrival/render
metrics are now printed alongside the existing steady-state metrics so startup
cannot disappear behind the two-second warm-up exclusion.

Android matched 5-second controls both delivered 300 frames and 240,000 PCM
sample-frames. Default preroll: audio output max39.99ms; no preroll: max3.68ms.
Video output maxima were9.49/9.43ms. This supports a measurement-sink startup
preroll hold rather than missing transport frames. Linux and Windows no-preroll
5-second controls also completed exact counts and continuous PCM. These are
short controls, not replacements for final integrated sustained qualification.

The earlier frozen Android 30-minute result is recorded in
[android-soak1800](android-soak1800/README.md), including startup-output and
historical-tail limitations. It must not be relabeled as a no-preroll run.
