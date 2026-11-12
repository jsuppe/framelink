# Paced authenticated decoder relay — focused Windows results

Test-only implementation at49f9145 (base84ad91d), independent static review complete. The local checker has **30 passing cases**, including all-frame freshness, metadata/PCM mapping, real-completion records, resource slopes, trace losses and cross-boundary identity mismatches. Focused local CTest1/1 passed0.20s. Local GStreamer was unavailable; this does not establish Windows compilation or runtime success.

## Contract

- Actual H.264 decoder produces NV12; explicit GPU conversion writes issued RGBA pool reservations. Decoder input matches target dimensions; tiny64×32 output uses320×240 input.
- Autonomous source pacing; no per-frame receiver control. Independent100Hz PCM shares the source schedule and preserves37ms video/audio media separation.
- Decoded pixels are not stamped or overwritten. Source and final receiver independently hash every active pixel row. Offline analysis requires all expected snow-frame digests distinct and matching, exact expected frame count/PTS, and unchanged decoder duration/absent DTS.
- Source records acquire/decode-pull/conversion/GPU-completion/oracle/submit phases. Final reader records actual copy/fence/lease completion and digest phases. Readback/conversion bytes mean active output payload, not total GPU memory traffic. Both source and receiver oracle copies are explicit measured work.
- Authenticated FAR4→snapshot/FAR5 preserves native allocation identity; final held-reader control proves upstream remains pending until genuine completion.
- Existing17/34ms whole-path arrival diagnostics and64KB/min,1handle/min,0.5thread/min resource gates remain unchanged. Short smoke/preflight results cannot qualify resource slopes.
- Default trace-off. Optional `--trace` is limited to5s with explicit bounded capacity, zero losses/rejected threads, exact role counts, and joins to actual native CSV identities across both boundaries. Transport traces do not expose every GStreamer/D3D operation, so they cannot prove absence of the explicit conversion/oracle copies.

`trace_checked` reports whether raw trace accounting was performed; `trace_qualified:false` avoids inferring complete rate qualification from that check. No physical-renderer or isolated-hop2ms acceptance claim.

## Windows results and next execution

At77afa1a the clean Windows build,9/9 focused tests and2s decoder smoke passed.
Full SHA256 now uses Windows CNG; all120 comparison samples and known vectors
matched the former GLib oracle. No pixels or timing gates were weakened.
Both5s1080p60 trace-off/on runs passed300 distinct decoded frames and240000PCM
frames with zero17ms arrival misses (max10.1737/11.0694ms).

The initial4K30 CNG preflight delivered all150 distinct frames and240000PCM
frames, but140 video arrivals exceeded34ms. Decoder-pull p50 was22.0109ms,
source readback/hash13.6742ms; no audio misses. Trace-on was not run.
Local causal investigation must separate on-demand fixture generation/encoding from
actual decode before any correction or repeat. No decoder endurance result,
physical presentation or isolated-hop budget is claimed. Initial GLib timing
and CNG integration-build failures remain archived.

## Explicit pre-encoded input control (b247e5d)

The source-only diagnostic measured1504K snow frames in5.14s for generation and
5.56s including encoding (quiet wall time; startup included). Separate instrumented
runs verified exact counts/PTS. This supports an upstream fixture contribution,
not an isolated decoder or transport finding.

`--prepare-fixture <file>` writes a versioned encoded-AU tape before measurement,
without opening FrameLink sessions. `--fixture <file>` explicitly selects replay;
the default on-demand generation/encoding path remains available. Tape records
preserve encoded payload, caps, PTS/DTS/duration, flags and offsets. The reader
checks the complete record structure and expected profile/count/PTS/footer/EOF
before the epoch, then checks reads again. A file-fed appsrc bounds its queue to
two AUs; there is also one feeder-held AU and decoder-internal storage. This is
not a two-frame total memory bound. No decoded frames are cached.

The runner must pin the entire tape SHA256 before and after replay. Actual D3D11
decode/conversion, all-pixel SHA256, native ownership, PCM and17/34ms gates remain
unchanged. Replay results must be labeled separately: they cannot waive the
on-demand failures. Initial Windows build validation is underway; no replay
performance or endurance success is claimed yet.
