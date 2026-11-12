# Native relay drain fix: focused validation and tiny decoder smoke

Exact source: `462fa5f192cd9b3347c798afa8108b9358716f7b`.
Run: `windows-native-ready-batch-prepared/runs/drain-fix-462fa5f-20261112t0806z`.
Actual capture: 2026-11-12 08:05 UTC (the run-name minute is not the capture clock).

- Configure/build **PASS**; exact five built-binary SHA256 pins in [status.json](status.json).
- Focused tests **8/8 PASS in 10.08s**, including pause **0.88s**, paused
  cancellation, existing bin, upstream/downstream cancellation, recovery,
  downstream death and native relay; see [focused-tests.log](focused-tests.log)
  and [LastTest.log](LastTest.log).
- Real decoder smoke **PASS**, two seconds, RGBA 64x32 at 30fps from 320x240
  H264 input: **60 distinct full-pixel SHA256 digests**, **96,000 PCM frames**,
  **zero video/audio arrival misses** under the existing 34ms diagnostic budget.
- [Decoder analysis](decoder-rate-analysis.log) records `pass:true`,
  `trace_checked:false`, `trace_qualified:false`. Source conversion, source
  oracle readback and final readback each account for **491,520 bytes**.
- Root independently reran the decoder checker successfully before requesting
  archival. The retained analysis log is the original captured Windows result;
  no extra independent-run receipt is invented here.
- [Final idle](idle-final.json): no active runtime processes at
  **08:05:52.2707000Z**. Controller and remote statuses both finished.

## Change and claim boundaries

This validates the helper-only `samples/native_relay.h` change at 462fa5f:
`NativeRelay::collect` stops once all missing native reservations are recovered,
while retaining generation checks and checks for each required acquisition.
It resolves the extra fully-reserved-ring probe established by the
[failed diagnostics](../diagnostics/README.md).

Runtime DLL source directories `src/`, `gst/` and `include/` have no diff between
production baseline `461584b` and this revision (verified locally during archive).
These are source-equivalence facts, not a claim that fresh DLL binary hashes are
identical to a prior build. The exact freshly built pins remain in status.json.

No public API change, freeze decision, sustained target-rate qualification,
long-run resource slope result, trace-on result or physical-renderer claim follows
from this focused run and tiny smoke. Resource samples are retained but the
2-second capture is too short for the unchanged long-duration slope gates.

## Original bytes and hash domains

All **32 entries** in the original [collection manifest](collected-sha256.json)
were verified before copying; raw role CSVs, summaries, stderr, configuration,
build/test/analysis logs, host, pins and idle evidence are preserved.
No source tar, executable, discovery credentials or unrelated payloads are copied.

- [artifact-sha256.json](artifact-sha256.json): original uploaded inputs; some
  referenced inputs, including source tar and scripts, are not archived here.
- [collected-sha256.json](collected-sha256.json): original Windows collection
  hashes, retaining the original path spelling and file bytes.
- [original-byte-sha256.json](original-byte-sha256.json): **34 copied files**,
  including the original collection manifest and controller status; excludes
  itself and this README.

Original CRLF/BOM and other bytes are retained. These are **pre-filter/original-byte
hashes, not Git-blob hashes**. Repository clean filters can rewrite environment
paths during staging; the parent owns any separate post-filter Git-blob manifest.
Original manifests must remain unchanged and distinct from that domain.
