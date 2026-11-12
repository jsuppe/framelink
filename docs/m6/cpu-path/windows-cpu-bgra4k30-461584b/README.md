# Windows shared-CPU BGRA 4K30 — exact candidate qualification

**PASS**, production source `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`. One five-second preflight followed by one 1800-second run; no repeats. Remote execution, collection, both raw-analysis implementations and independent parent raw-data analysis succeeded. All four production binaries remained hash-identical before, between and after the cases and at the final idle check. This is not the startup diagnostic overlay.

## Frozen profile and results

Shared CPU BGRA 3840×2160 at 30fps; pool 2, unread queue 1, transport latency 0, transport-only, no output preroll, output queue 1, raw accounting, tracing/fault injection off. Exact invocations are archived in each case's `command.json`.

| Measure | Five-second preflight | 1800-second qualification |
| --- | ---: | ---: |
| Video frames, produced/received | 150/150 | 54000/54000 |
| PCM sample frames | 240000 | 86400000 |
| Contiguous audio blocks | 500 | 180000 |
| Video arrival max | 7.481384ms | 10.7371ms |
| Audio arrival max | 2.06655ms | 4.0462ms |
| Video/audio 34ms misses | 0/0 | 0/0 |
| Inferred common A/V mapping delta | 0ns | 0ns |

Sustained video arrival p95: 6.398034ms. All-frame timing includes startup and drain. Render and push timing also have zero 34ms misses; full reports are retained. Resource regressions use the original steady-state second-half method and rounded resource observations, excluding warm-up and final teardown:

| Process | Private memory KB/min | Handles/min | Threads/min | CPU |
| --- | ---: | ---: | ---: | ---: |
| Consumer | 7.166158 | -0.006366 | -0.001110 | 12.268555% |
| Producer | 7.388657 | 0 | -0.001171 | 15.432896% |
| Limits | 64 | 1 | 0.5 | No additional CPU cap |

Growth limits apply to the sustained run, not the five-second preflight. Positive memory slopes below the existing limit are not relabeled zero growth.

Host: Windows 11 Home build26300, AMD Ryzen7 9800X3D (8 cores/16 logical), 68305281024 bytes physical RAM, RTX5090 driver32.0.16.1714. See `host-final.json` for machine and final binary evidence. Sustained run started 06:55:45Z on2026-11-12; runtime idle verified07:25:52.9344762Z. Windows ownership was then released; no additional campaign was launched by this worker.

## Evidence and scope

Scripts, exact commands, reports, summary/resource-series logs and SHA256 manifests are archived here. The original complete evidence remains at:

`<linux-home>/dev/evidence/m6-cpu-path/windows-cpu-4k-461584b-prepared/runs/bgra-4k30-20261112t0655z`

The eight raw CSV files are under that directory's `data/` case folders; they are not duplicated in this compact archive. All **58** original manifest entries were rehashed successfully, including all eight CSVs (**12617396 bytes**); see `raw-hash-verification.json`. `evidence-sha256.json` retains paths relative to the original evidence root, not this archive. `archive-sha256.json` separately hashes the archived files. `parent-verified-analysis.json` was regenerated independently from the retained raw series without rerunning Windows.

`run.py` is the original generic single-format controller; the executed format/run ID are fixed in `controller-status.json` and `config.json`. `artifact-sha256.json` describes the uploaded runner files. Retaining a runner is not authorization to rerun it or take the runtime lane.

This establishes this shared-CPU transport profile's accounting, timing and resource gates. It does **not** establish payload hashes, physical playback, native allocation identity, native two-hop rate qualification or completion of M6. Prior startup/native-lifetime failures remain separate and are not waived. API not frozen; M7 held.
