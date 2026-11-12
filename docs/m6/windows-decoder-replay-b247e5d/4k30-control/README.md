# First 4K30 replay control — overall pair FAIL

Exact source `b247e5d11b3ba540f78beeef172874f0606e83fc`.
Original run: `windows-decoder-replay-target-prepared/runs/replay-4k30-b247e5d-20261112t0818z`.

Preparation **PASS**: 150 encoded access units, **650,192-byte** tape,
SHA256 `5c3797a3fe9d4c86992cd3be15627fa18bb184334cb557606b3224c30554cb1e`.
The tape was independently hash-verified during archival but remains external
as `fixture.flau` in the original run directory; it is **not in this repository**.
Exact inspections, caps and provenance remain archived.

| Five-second replay arm | Functional/identity result | 34ms arrival gate |
| --- | --- | --- |
| Trace off | 150 distinct frames / 240,000 PCM, exact terminal accounting | PASS, zero video/audio misses; max 30.735534ms |
| Trace on | Same exact counts; trace identity/loss/copy checks PASS | FAIL, video identities 116 and 117 missed; max 36.6673ms; zero audio misses |

Trace-on source oracle readback/hash maximum: **31.6217ms**. This combined phase
observation is not isolated causal proof. `trace_checked:true` describes passing
raw trace checks only; it does not override the failed timing result.
**Overall pair FAILED; no endurance run.** No physical-renderer qualification.

The original controller's `remote outcome/revision mismatch` collection error
is retained: remote exit was 1 because the traced arm failed its timing gate.
It must not be mistaken for a passing capture with only cosmetic collection
trouble. Final idle: **2026-11-12T08:17:36.936Z** (full precision in receipt).
No performance rerun occurred during archival.

## Preserved original bytes

All **63 collection hashes**, **6 uploaded-input hashes**, and the external tape
hash/length verified before copying. Raw CSVs/traces, phase analysis, configs,
scripts, terminal summaries, tape inspections, pins, host and idle receipts are
retained. `original-byte-sha256.json` covers **69 copied files**, excluding itself
and this README. No tape, executable or credential was copied.

Original manifests/CRLF/BOM bytes remain unchanged. These are **pre-filter byte
hashes, not Git-blob hashes**; a parent-added post-filter manifest is a separate
domain. No Windows access or execution occurred during archival.
