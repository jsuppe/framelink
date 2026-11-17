# Initial 4K30 CNG decoder preflight — FAIL

Exact source `77afa1a4e9abcbdfdcae007d3d6dfb4dac97763f`.
Original run: `windows-decoder-rate-target-prepared/runs/decoder-4k30-cng-77afa1a-20261112t0812z`.

Five-second trace-off capture completed **150 distinct full-pixel identities** and
**240,000 PCM frames**, with exact terminal counts. Executable success does not
constitute a rate pass: analyzer **FAIL**, with **140/150 video arrivals over the
unchanged 34ms budget** (identities **10 through 149 inclusive**). Audio misses: **0**.

- Maximum scheduled-to-final video arrival: **654.186767ms**.
- Source decoder pull wait p50: **22.0109ms** (not isolated decoder processing time).
- Source oracle readback+hash p50: **13.6742ms**.
- Final staging map+hash p50: **13.0011ms**.

These phase observations do not establish a complete causal attribution or a fix.
[Original analysis](trace-off-analysis.log) and all raw role CSVs are retained.
**Trace-on NOT RUN** after failed trace-off; **duration NOT RUN**.
`trace_checked:false`, `trace_qualified:false`. The successful 1080p pair does not
qualify this 4K target. No endurance or physical-renderer result is implied.
Final idle: **2026-11-12T08:11:11.0736704Z**.

All **32 original collected hashes** and **5 uploaded-input hashes** were verified
before copying. `original-byte-sha256.json` covers **34 copied files**, excluding
itself and this README. Original manifests and CRLF/BOM bytes are preserved;
these are pre-filter original-byte hashes, **not Git-blob hashes**. Any parent-added
post-filter Git manifest is a separate domain. No binary, source tar or credential
was copied; no Windows contact occurred during archival.
