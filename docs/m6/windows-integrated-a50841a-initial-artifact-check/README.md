# Initial integrated runner: artifact-presence check failure

Exact source `a50841aad44fbc9431234a4f326bad036d193bf5`.
Original run: `windows-final-integrated-prepared/runs/final-windows-a50841a-20261112t0819z`.

This is a **runner verification failure, not a code build/test failure**.
All configure/build commands exited **0**, including the full build and each
missing-target build batch. The runner then stopped with
`configured target artifacts still missing`.

[Artifact diagnosis](artifact-presence-diagnosis.json) establishes:

- 196 declared artifacts inspected.
- 97 missing entries, **all `.pdb`** (optional Release debug symbols).
- All 99 non-PDB entries present.

**No CTest, API-check or baseline-client stage ran.** Their absence is not a pass
or failure result. The original [status](results/status.json), scripts, build logs,
configuration and controller failure are preserved without retroactive edits.
A future runner filter correction was being prepared separately at archival
handoff; this archive contains no corrected-run result.

Final idle receipt: **2026-11-12 08:19:45.579Z** (full precision in `host/`).
No Windows access or rerun occurred during archival.

## Original-byte evidence

All **26 collected hashes** and **7 uploaded-input hashes** were verified before
copying. Source tar was verified in place but omitted; no build tree, executable
or credentials copied. Compact original scripts/config/references, logs, host
receipts, artifact diagnosis, invocation and controller are retained.

`original-byte-sha256.json` covers **31 copied files**, excluding itself and this
README. Original uploaded/collected manifests and CRLF/BOM bytes remain unchanged.
These are **pre-filter original-byte hashes, not Git-blob hashes**. Any parent-added
post-filter Git-blob manifest is separate because clean filters may rewrite paths.
