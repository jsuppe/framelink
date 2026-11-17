# Initial oracle diagnostic setup — overlay byte-hash rejection

Original run: `windows-decoder-oracle-phase-prepared/runs/oracle-phase-a50841a-20261112t0834z`.
Base source: `a50841aad44fbc9431234a4f326bad036d193bf5`.

The fresh-source hash check passed before patching. `git apply` exited **0**, but
the overlay source-byte verification correctly rejected the result. **No build,
media capture or diagnostic measurement ran.** Final idle:
**2026-11-12T08:34:24.240855Z**.

[Root's read-only comparison](root-source-mismatch-diagnosis.json) found only
`test/fl_gst_local_decoder_rate_win.cpp` mismatched:

- Expected: 33,179 bytes, 161 LF line endings, no CRLF.
- Actual: 33,340 bytes, 161 CRLF line endings.
- Identical after CRLF-to-LF normalization.

This identifies Git platform line-ending conversion, not a measured media defect.
The future setup correction uses per-command `core.autocrlf=false` and
`core.eol=lf`; the exact-byte hash check remains enforced, not relaxed. No corrected
setup or runtime success is claimed by this failed attempt.

Original prepared source, patch, scripts, config, provenance and results are under
`data/`, including the unchanged original plan README. That plan is not evidence
that its intended runtime steps executed. Controller failure remains intact.
The diagnosis records the actual source hash/length; the archived diagnostic.cpp
is the intended LF overlay, not a falsely relabeled copy of the CRLF result.

## Hash domains

Verified **27/27 original collection hashes** and **17/17 uploaded-input hashes**,
including source tar verified in place but excluded. No tar, binary or credential
is copied. `original-byte-sha256.json` covers **30 copied files**, excluding itself
and this README, preserving original bytes. `git-blob-sha256.json` separately hashes
clean-filtered committed archive blobs, excluding itself. Original manifests are
unchanged; original-byte and Git-blob hashes must not be interchanged.

All archival work was local. No Windows contact or rerun occurred.
