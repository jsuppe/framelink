# 4K30 queue control — FAIL

Source `c539d99e538a37a667582fd1d8120d936573792b`.
Original run: `windows-decoder-rate-target-prepared/runs/decoder-4k30-auqueue-c539d99-20261112t0814z`.

Five-second trace-off capture completed **150 distinct frames / 240,000 PCM frames**
with exact terminal accounting. Analyzer **FAIL**: **118 video arrivals exceeded
34ms**, with **zero audio misses**. Maximum video age: **542.183067ms**.
Decoder pull wait p50: **21.7833ms**; source oracle readback/hash p50: **13.6972ms**.
These combined phases are not isolated decoder execution or hash-only timings.

Queue correction alone was insufficient to meet the existing target gate.
At archival handoff the next investigation was source generation/encoding
throughput; no further causal conclusion or repaired rate result is implied.
**Trace-on not run; endurance not run.** Final idle:
**2026-11-12T08:13:24.7934383Z**.

Raw video/audio/resource CSVs, role summaries, exact config, binary pins, scripts,
analysis, statuses, host and idle receipts are retained. Original manifests verified:
**32/32 collected files**, **5/5 uploaded inputs**. `original-byte-sha256.json`
covers **34 copied files**, excluding itself and this README. These preserve
original CRLF/BOM bytes and are **not Git-blob hashes**. Any parent post-filter
Git-blob manifest is separate; original manifests remain unchanged. No executable,
source tar or credentials copied; no Windows contact during archival.
