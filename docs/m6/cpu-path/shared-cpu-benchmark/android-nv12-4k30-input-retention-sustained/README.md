# Android NV12 4K30 input-retention sustained

Source 93283ac; docs d6f9b4b; binary dee5c211a99e0091410ee6f8a84c7e6711124ef49b8d276058d9a50d1daa43dd. 1800s exit/pull/analysis 0. Revalidated 54000 video / 86400000 PCM at both endpoints; 180000 contiguous audio blocks. Zero 34ms video/audio misses including startup/drain. Video p95 12.530724ms, max 21.870670ms; inferred common A/V mapping delta 0ns. RSS consumer -1363.99 / producer -1280.45 KB/min; handles 18..19/17, threads 8..9/6..7 bounded. Transport-consumer scope, not physical presentation or independent identity proof.

Raw manifest base: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-nv12-3840x2160-30-1800-input-retention

BGRA4K and NV121080 remain unqualified pending matched copy/offCPU diagnosis.
