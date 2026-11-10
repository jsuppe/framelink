# Android NV12 1080p60 shared-CPU cache endurance

Revision 4f37f25; 1800 seconds. Exact 108000 video and 86400000 PCM frames, 180000 contiguous audio blocks. Zero video/audio 17 ms diagnostic misses, video p95 8.077569 ms and maximum 12.205847 ms. Inferred A/V mapping delta 0 ns; RSS slopes -22.41 KB/min both endpoints, handle/thread slopes nonpositive. Benchmark/controller/analysis exit 0. Includes startup/drain; inferred mapping is not physical playback or identity proof.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-nv12-1920x1080-60-1800-cache-warm

BGRA startup misses remain unresolved. Linux affected gates require refresh. M6 remains open.
