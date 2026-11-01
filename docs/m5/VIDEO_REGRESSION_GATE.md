# M5 video performance regression gate

User requirement 2026-10-30: do not advance milestone while previously achievable
video frame rates are unverified or regressed. Functional A/V success is insufficient.

## Comparison protocol
- Record revision, hardware/device, format, dimensions, requested FPS, clocks,
  copy/conversion path, scene, display/preview mode, tracing and competing loads.
- Compare a known prior version and candidate under matching conditions, including
  the legacy video-only path; separately add audio and external decode.
- Begin Windows BGRA/NV12 1080p30/60, then previously qualified higher resolutions
  using their actual baseline target. Do not label old frame counts measured FPS.
- Measure warmup separately; repeat steady-state windows, at least60s where finite
  protocol permits. Short finite playback segments need startup/gaps reported;
  repeated sessions are not uninterrupted steady-state streaming.
- Track source IDs/PTS, producer output, consumer arrivals, renderer adoptions,
  submit/present feedback if available, and browser image updates separately.
- Report unique fresh FPS, skipped/duplicate frames, interval p50/p95/p99/max,
  queue depth/latency and CPU/GPU cost. Callback count is not unique output FPS.
- Compare preview disabled/enabled and tracing disabled/enabled. Do not use repeated
  screenshots or per-frame hashes during the uninstrumented performance baseline.
- Present/scanout unavailable means unqualified, never infer from submission.

## Exit criteria
No unexplained loss versus matched baseline; required target rates must hold
without hiding latency or queue growth. Set numerical regression tolerances from
repeatability measurements before evaluating candidate, not after seeing a loss.
A low baseline is not sufficient if it misses the user's required established rate.
Keep frame correctness and unchanged A/V timing gates alongside throughput.
No M5 completion or M5a advancement until this gate is resolved/documented with user.
