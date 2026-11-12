# Decoder queue correction: build and tiny smoke

Exact source `c539d99e538a37a667582fd1d8120d936573792b`.
Original run: `windows-native-ready-batch-prepared/runs/decoder-auqueue-c539d99-20261112t0813z`.

- Configure/build **PASS**.
- **9/9 focused tests PASS in 10.18s**: SHA256 rows, upstream/downstream
  cancellation, recovery, pause, paused cancellation, existing local bin,
  downstream death and native relay.
- Two-second tiny decoder smoke **PASS**: **60 distinct full-pixel identities**,
  **96,000 PCM frames**, zero video/audio arrival misses; maximum scheduled video
  arrival **7.7894ms**. Trace off.
- Exact binary pins and command/status records preserved. Final idle:
  **2026-11-12T08:13:06.1648462Z**.

See [focused-tests.log](focused-tests.log), [LastTest.log](LastTest.log),
[decoder analysis](decoder-rate-analysis.log) and all raw role CSVs under `smoke/`.
This is a tiny functional/smoke result, not a target-rate or endurance result.
The separate 4K target control was pending at archival handoff and is not included.
No trace-on, physical-renderer or whole-milestone qualification is claimed.

## Original bytes

All **32 collected hashes** and **5 uploaded-input hashes** verified before copying.
The uploaded source tar was verified in place but is deliberately not archived;
no binaries or credentials were copied. Compact host, config, scripts, build/test
logs, raw smoke, summaries, idle and controller status are retained.

Original `artifact-sha256.json` and `collected-sha256.json` are unchanged.
`original-byte-sha256.json` covers **38 copied files**, excluding itself and this
README. CRLF/BOM bytes are preserved. These are original pre-filter byte hashes,
**not Git-blob hashes**. The parent owns any separate post-filter Git-blob manifest
because repository clean filters may rewrite environment paths at staging.
No Windows contact, execution or repository commit occurred during archival.
