# Replay-capable decoder fixture: build and on-demand smoke

Exact source: `b247e5d11b3ba540f78beeef172874f0606e83fc`.
Original run: `windows-native-ready-batch-prepared/runs/decoder-tape-b247e5d-20261112t0817z`.

- Configure/build **PASS**.
- **9/9 focused tests PASS in 10.09s**; exact results in
  [focused-tests.log](focused-tests.log) and [LastTest.log](LastTest.log).
- Two-second tiny **on-demand** decoder smoke **PASS**: **60 distinct full-pixel
  identities / 96,000 PCM frames**. Exact config, role CSVs and summaries retained.
- Root independently reanalyzed the smoke successfully before archival; the
  retained [analysis log](decoder-rate-analysis.log) is the original Windows
  analysis, not an invented independent-run receipt.
- Exact binary pins in [status.json](status.json). Final idle:
  **2026-11-12T08:17:01.8035172Z**.

This checks the replay-capable revision's build and existing tiny on-demand path.
It does **not** establish replay performance, 4K target rate, a matched trace pair,
endurance, or physical playback. The first 4K replay preparation/pair was running
separately at archival handoff and is not represented by this evidence.

## Original-byte provenance

Verified **32/32 original collected hashes** and **5/5 uploaded-input hashes**.
The source tar was verified in place but deliberately not copied. Retained compact
raw smoke, scripts, logs, config, host, pins, controller status and idle evidence;
no binary or credential was copied. `original-byte-sha256.json` covers **38 copied
files**, excluding itself and this README.

Original `artifact-sha256.json` and `collected-sha256.json` remain unchanged.
These preserve original CRLF/BOM bytes and describe **pre-filter byte hashes,
not Git-blob hashes**. The parent owns any separate post-filter Git-blob manifest
because repository clean filters may rewrite environment paths during staging.
No Windows access or execution occurred during archival.
