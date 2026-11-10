# Linux NV12 4K30 cache endurance

Revision 4f37f25, 1800s: exact 54000 video/86400000 PCM, 180000 contiguous audio blocks. Zero 34ms video/audio diagnostic misses; video p95 10.594895ms, max 11.469009ms, inferred A/V mapping delta 0ns. RSS 3.23KB/min both, handle/thread slopes nonpositive. Exit0/analysis0. Includes startup/drain; no independent identity or physical playback claim.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/linux-shared-nv12-3840x2160-30-1800-cache-warm
