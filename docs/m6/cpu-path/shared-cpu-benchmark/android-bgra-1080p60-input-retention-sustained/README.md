# Android BGRA 1080p60 input-retention sustained

Source 93283ac, docs 97f8be1, production binary dee5c211a99e0091410ee6f8a84c7e6711124ef49b8d276058d9a50d1daa43dd. 1800s exit/pull/analysis 0; 108000 video and 86400000 PCM frames, 180000 contiguous audio blocks. Zero 17ms video/audio misses including startup/drain; video p95 9.366368ms, max 15.559668ms, inferred common A/V mapping delta 0ns. RSS -24.51KB/min both; handles 18..19/17, threads 8..9/6..7 bounded. Transport-consumer scope; not independent identity or physical playback proof. Android BGRA4K remains suspended pending matched copy/off-CPU control.

Raw evidence and manifest base: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-bgra-1920x1080-60-1800-input-retention
