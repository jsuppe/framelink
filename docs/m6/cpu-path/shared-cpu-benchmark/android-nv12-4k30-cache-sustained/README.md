# Android NV12 4K30 shared-CPU cache endurance

Revision 4f37f25; 1800 seconds. All 54000 video frames and 86400000 PCM frames accounted for; 180000 contiguous audio blocks. Zero video/audio 34 ms diagnostic misses, video p95 12.381590 ms, maximum 25.010270 ms; inferred A/V mapping delta 0 ns. RSS slopes consumer 0.00 / producer 12.50 KB/min (<64); handle/thread slopes nonpositive. Controller, benchmark and analysis exit 0. Includes startup/drain; inferred mapping is not physical playback or identity proof.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-nv12-3840x2160-30-1800-cache-warm

Other pre-cache cells require refresh; BGRA startup misses remain unresolved. M6 is not complete.
