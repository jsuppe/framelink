# Initial native pause validation failure — 9ea51d4

Exact source: `9ea51d4764b9b771b780555e7a23642976f279b6`.
Captured 2026-11-12 08:02 UTC; archived locally without re-execution.

- Configure/build **PASS** (`configure_exit=0`, `build_exit=0`).
- Focused tests **2/3 PASS**, overall CTest exit **8**.
- `gst_relay_local_bin_pause_win` **FAIL**, 2.43s: line 444,
  `g_get_monotonic_time()<deadline&&video.collect()==FL_OK`, while collecting
  final pending native work. This assertion alone does not distinguish timeout
  from collection error; no causal fix is established by this evidence.
- `gst_relay_local_bin_paused_cancel_win` **PASS**, 0.88s total;
  source cancellation propagated in **97.416ms**, source observation **0.047ms**.
- Existing `gst_relay_local_bin_win` **PASS**, 1.14s, including authenticated
  three-process native+PCM forwarding, exact pixels/metadata and final lease.
- Decoder-rate smoke **NOT RUN**: the runner stopped at the focused-test failure.
- Final idle receipt records no active runtime processes. Controller failed,
  but packaging and collection both succeeded. No repair/revalidation result
  is included or implied.

## Contents and provenance

[status.json](status.json) preserves source revision, exact focused command and
five built-binary SHA256 pins. [LastTest.log](LastTest.log) preserves individual
assertion/output/timings; [focused-tests.log](focused-tests.log) is the CTest
summary. Build/configure, inventory, host details, controller status and final
idle receipts are retained alongside the configuration and original manifests.
No source tar, binary, discovery credential, or unrelated artifact was copied.

Source collection directory:
`windows-native-ready-batch-prepared/runs/ready-9ea51d4-20261112t0802z`.

Hash domains are deliberately distinct:

1. [artifact-sha256.json](artifact-sha256.json) is the original uploaded-input
   manifest, including the source tar and scripts that are **not** archived here.
2. [collected-sha256.json](collected-sha256.json) is the original Windows
   collection manifest. All **15** listed raw files were independently verified
   before copying, preserving bytes including CRLF/BOM where present.
3. [original-byte-sha256.json](original-byte-sha256.json) hashes all **17** copied
   files (the 15 raw files, original collection manifest and controller receipt).
   It excludes itself and this explanatory README.

These hashes describe original/local byte content, **not Git blobs**. The
repository clean filter may rewrite environment paths during staging. Retain
these original manifests unchanged; any post-filter Git-blob manifest belongs
in a separate hash domain and must not be substituted for original-byte hashes.
