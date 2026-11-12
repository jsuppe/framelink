# Integrated Windows candidate a50841a — PASS, not frozen

Exact source **`a50841aad44fbc9431234a4f326bad036d193bf5`**.
Original run: `windows-final-integrated-prepared/runs/final-windows-a50841a-toolpath-20261112t0822z`.

- Clean build and required artifact verification **PASS**.
- **178/178 CTests PASS, zero skips, 665.56s**. Exact inventory, JUnit and
  [full log](results/full-ctest.log) retained; [local coverage](local-ctest-coverage.json)
  independently matches executed tests to inventory.
- Both DLL export sets **PASS**: **128 expected exports**, retaining all **119**
  baseline names, no missing or unexpected names. See [local comparison](local-api-comparison.json).
  Export-name compatibility is not arbitrary ordinal ABI proof.
- All four candidate GStreamer element inspections completed from the candidate
  plugin: framelinksink, framelinksrc, framelinkavsink and framelinkavsrc.
- **3/3 unchanged 7b8a274-built clients PASS** against these DLLs: metadata,
  native epoch C API and D3D11 read lease. Client hashes/imports and candidate
  before/after hashes are retained. [Local compatibility check](local-compatibility.json).
- Final idle **2026-11-12T08:33:27.7018315Z**; controller and remote finished.

This integrates the checked helper/test additions with unchanged runtime DLL
source contracts. It is a Windows app-integration **candidate**, not API freeze,
full M6 closure, sustained decoder qualification, lifetime acceptance or physical
playback proof. Existing D2/D3 decisions, failed timing cells and suspended
startup/resource gates remain open. M7 remains held.

## Prior runner failures remain evidence

The first [artifact-check failure](../windows-integrated-a50841a-initial-artifact-check/README.md)
was optional Release PDB classification; builds had succeeded and tests had not run.
The second [tool-path failure](../windows-integrated-a50841a-toolpath-failure/README.md)
was gst-inspect executable resolution before tests/API comparison/clients.
This successful run retains its own exact scripts and does not overwrite either
failure or claim their unexecuted stages passed.

## Provenance and hash domains

Original scripts/config/references, commands, all logs/raw JUnit, host, exports,
client checks, controller and local independent verification are preserved.
No source tar, evidence tar, binaries or build tree included.

- Original uploaded-input manifest: **7/7 verified**, including source tar verified
  in place and intentionally excluded from the archive.
- Original collection manifest: **44/44 hashes verified**, with complete coverage
  of collected files (excluding the collection manifest itself).
- `original-byte-sha256.json`: **51 copied files**, excluding itself and README.
  Original CRLF/BOM bytes preserved.
- `git-blob-sha256.json`: separate hashes of staged, clean-filtered archive blobs,
  excluding itself. Paths and line endings may differ from original bytes.

The original manifests remain unchanged. Original-byte hashes are not Git-blob
hashes. All archival/verification work was local; no Windows rerun occurred.
