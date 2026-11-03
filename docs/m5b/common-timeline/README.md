# M5b.5 common timeline — partial qualification

Task5 remains OPEN. Shared segment conversion and receiver mapping are implemented
for public audio and context-enabled video. Coordinated video requires explicitly
adopted channels, exact negotiated geometry, generation-bound metadata and shared
local track readiness. Legacy video without a context keeps its existing behavior.
No state callback waits for a sibling track. Consumer mapping is required for
coordinated A/V; missing/conflicting/stale mappings reject rather than retimestamp.

Audio normalizes forward rate1 TIME segments to running time and transports a
canonical TIME segment. DTS and GAP follow the same conversion. Consumer mapping
is common across both tracks, never based on each first frame. F32/44.1kHz1000-
sample blocks and DISCONT verify cumulative rounding: the validator permits at
most1ns quantization difference against cumulative sample arithmetic, never
rewrites the transmitted PTS/duration or allows accumulated timing drift.

Linux7/7 and Windows7/7 targeted checks passed. Android4/4 executables passed,
arm64/x86_64 native and JNI builds passed. Integrated public elements send10frames
per case with complete PCM/pixels/PTS/DTS/duration/generation. Audio uses a2s
segment start, video5s, both base500ms, shared receiver mapping adds100ms. Audio
leading and lagging each preserve exactly37ms. Missing receiver mapping rejects.
An audio50ms GAP and discontinuity preserve their mapped times, not silence.

Coordinated EOS carries submitted video count/final running PTS in the audio
terminal record after both local tracks end. Consumer video drains and verifies
its matching count/last PTS; peer death is not substituted for EOS. These nominal
cases do not qualify zero-media EOS or loss-policy drain behavior yet.

Remaining task5 work: ordered video GAP/control propagation, coordinated flush and
fresh-epoch behavior, additional invalid/overflow/seek/event negatives, zero-media
terminal behavior, and final review. Full pause/reconnect/security matrix remains
task6. CPU-readable video is used in this timing fixture; no native performance,
cross-UID public-element or physical-output qualification is implied.
