# Windows shared-CPU benchmark integration

The benchmark now permits explicit `--shared-cpu` on Windows after native Windows allocator, authenticated endpoint, local coordinator and bins were implemented. Defaults and invalid combinations remain unchanged. Two Windows CTest cases exercise independent process BGRA/NV12 CPU bins with a two-slot pool, one-frame unread queue and zero configured transport latency. Linux/Android code behavior is unchanged; no deferred campaigns were launched.

Windows affected suite **4/4 passed**: CPU A/V contract, native benchmark smoke, BGRA and NV12 CPU benchmark smokes. Each CPU benchmark delivered 150 video / 240000 PCM frames, contiguous audio and complete output accounting. A separate traced pass recorded exactly 150 explicit sink copies and 150 explicit source copies per format: 2457600 bytes each for BGRA, 921600 each for NV12. This is two CPU copies, not zero-copy. Timing and resource values from five-second traced smokes are not qualification.

Independent gst-launch processes at the prior d60f683 library revision delivered 32 exact 64x64 frames and 25920 stereo PCM frames per format, both exit0. Full lengths and SHA256 match separate producer tee reference files. First driver attempts failed Windows executable lookup and then backslash parsing of file paths; these are retained. The corrected driver uses an absolute executable and forward-slash GStreamer paths. No product fix was needed for these driver errors.

Raw frames/traces: <linux-home>/dev/evidence/m6-cpu-path/windows-cpu-benchmark. Sources were d60f683 plus the two manifest-recorded benchmark/CMake changes. Loom briefing could not load because chromadb is unavailable. Native producer integration, Windows lifecycle/timing and full-rate acceptance remain open. API not frozen; M6 open; M7 hold.

## Trace-off target preflight and owned sustained validation

Revision76a65d2, BGRA1920x1080@60 for5s:300video/240000PCM,500contiguousaudio blocks; no17ms video/audio misses, video maximum4.107267ms, inferred common A/V mapping0ns. Preflight reports are in preflight/. This does not establish endurance.

The sole 1800s validation at **76a65d2** finished with remote/pull/analysis exit0. Independently re-analyzed all 108000 video and 180000 contiguous audio rows; 86400000 PCM sample frames, no 17ms video/audio misses. Video p95 3.272417ms / max 5.595817ms; audio max 4.676150ms. Inferred common A/V mapping delta0ns (not physical playback or allocation-identity proof). Consumer/producer private-memory slopes -0.44/2.92KB/min, independently reconstructed from the raw series and below64KB/min; CPU6.44/8.20%. All four binary hashes match the initial run. Reports are in sustained/; raw CSVs remain in `<linux-home>/dev/evidence/m6-cpu-path/windows-cpu-benchmark/sustained/data`. No test was repeated. Other Windows formats/resolutions and native-producer/lifecycle/timing acceptance gates remain open.
