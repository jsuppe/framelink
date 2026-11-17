# Android BGRA4K30 affinity-only ABBA control

All four sequential 120s cells independently revalidated: exit/pull0, complete EOS, 3600 video and 5760000 PCM frames at both endpoints, 12000 contiguous audio blocks, exactly 3600 unique sink and consumer stage indices, no output-cache fallback. Binary 0f1e89a4 based on production 93283ac; commands and effective thread affinity captured per cell. Raw evidence and SHA256 paths are in manifest.sha256. Stage joins use consumer seq-1; arrival mapping is inferred, not independent identity proof.

| Cell | Allowed CPUs | Video misses >34ms | Arrival p95/max ms | Copy wall p95 us | Copy CPU p95 us | Wall-minus-CPU p95 us |
|---|---|---:|---|---:|---:|---:|
| A1 | 0-7 | 1 | 21.642 / 34.623 | 13374 | 12263 | 3888 |
| B1 | 6-7 | 0 | 21.119 / 28.832 | 13292 | 10983 | 7246 |
| B2 | 6-7 | 1 | 21.244 / 34.009 | 13529 | 11436 | 5693 |
| A2 | 0-7 | 0 | 21.609 / 29.488 | 13548 | 12664 | 3781 |

All audio has zero budget misses and common inferred A/V mapping delta0ns. Both affinity groups retain a miss; big-core restriction lowers median CPU cost but does not remove tail wall cost and increases p95 wall-minus-CPU. The separate big-core 5s preflight retains four misses. No scheduler-specific root cause, hardware limit, affinity production change, or qualification is inferred. Full late-frame neighbors are retained in validated-stage-analysis.json. CPU time uses getrusage and can have accounting granularity error; wall-minus-CPU is descriptive, not a scheduler trace.

Next bounded mechanism: current source copy-out calls memcpy once per row even when both source and destination planes are packed. Compare only consumer whole-plane memcpy versus row copies with identical diagnostic instrumentation and all-core affinity, validating counts, stage coverage and exact pixels before any production change. Preserve padded-stride row copies, byte reporting, metadata and ownership. No further affinity repeats or blind endurance reruns. BGRA4K and NV121080 acceptance remain open.

Device CPU0–5 capacity516/1.8GHz, CPU6–7 capacity1024/2.0GHz. Process-wide restriction can create big-core contention; ABBA does not eliminate chronological/thermal confounding. No cross-format conclusion for NV121080.
