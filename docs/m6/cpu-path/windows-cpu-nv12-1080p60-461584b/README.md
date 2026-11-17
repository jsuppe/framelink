# Windows shared-CPU NV12 1080p60 — exact candidate qualification

**PASS**, source `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`. One five-second preflight followed by one 1800-second run; no repeats. Remote, collection, worker analysis and independent parent raw-data analysis all succeeded. Four production binaries remained hash-identical; this is not the startup diagnostic overlay.

## Frozen profile and results

Shared CPU NV12 1920×1080 at 60fps; pool 2, unread queue 1, transport latency 0, transport-only, no output preroll, output queue 1, raw accounting, tracing/fault injection off. Exact invocation is archived in each case's `command.json`.

| Measure | Five-second preflight | 1800-second qualification |
| --- | ---: | ---: |
| Video frames, produced/received | 300/300 | 108000/108000 |
| PCM sample frames | 240000 | 86400000 |
| Contiguous audio blocks | 500 | 180000 |
| Video arrival max | 4.0679ms | 4.554967ms |
| Audio arrival max | 2.0695ms | 2.6284ms |
| Video/audio 17ms misses | 0/0 | 0/0 |
| Inferred common A/V mapping delta | 0ns | 0ns |

Sustained video arrival p95: 3.326634ms. Render and push timing also have zero 17ms misses; full independent reports retained. Resource regressions use the existing steady-state second-half method, not an invented nonpositive-growth requirement:

| Process | Private memory KB/min | Handles/min | Threads/min | CPU |
| --- | ---: | ---: | ---: | ---: |
| Consumer | 7.232851 | 0.001563 | -0.001165 | 5.674491% |
| Producer | 7.568537 | 0 | -0.001088 | 5.966288% |
| Limits | 64 | 1 | 0.5 | No additional CPU cap |

Host: Windows 11 Home build26300, AMD Ryzen7 9800X3D (8 cores/16 logical), 68305281024 bytes physical RAM, RTX5090 driver32.0.16.1714. See `host-final.json` for machine and binary evidence. Sustained run started 06:24:34Z on2026-11-12; runtime idle verified06:54:44.4516275Z.

## Evidence and scope

Scripts, exact commands, reports, summary logs and SHA256 manifests are checked in here. Complete raw CSV/resource series remain at `<linux-home>/dev/evidence/m6-cpu-path/windows-cpu-nv12-1080p60-461584b/data`; hashes are in `evidence-sha256.json`. `parent-verified-analysis.json` was independently regenerated from those final raw series, without rerunning Windows.

This establishes this shared-CPU transport profile's accounting, timing and resource gates. It does **not** establish payload hashes, physical playback, native allocation identity, native two-hop rate qualification, or completion of M6. Older startup/native-lifetime failures remain suspended, not waived. API not frozen; M7 held.
