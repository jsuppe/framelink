# Second integrated runner failure: GStreamer tool resolution

Exact source `a50841aad44fbc9431234a4f326bad036d193bf5`.
Original run: `windows-final-integrated-prepared/runs/final-windows-a50841a-symbolfix-20261112t0821z`.

Configure/build, corrected artifact verification, **178-test inventory** and both
DLL export dumps completed successfully. An inventory is **not executed tests**;
export dumps alone are **not API comparison results**.

The runner then failed at `inspect-framelinksink`, attempting Python `Popen` with:

```text
gst-inspect-1.0 framelinksink
[WinError 2] The system cannot find the file specified
```

This is a **runner tool-path resolution failure**, not a reported plugin, build or
test failure. **No CTest, API-comparison or baseline-client stage ran.** The future
runner uses an absolute `gst-inspect-1.0.exe` path and early tool preflight; this
archive does not contain that future run's result.

Original [status](results/status.json), scripts, configuration, logs, exports,
inventory, hashes and host evidence are preserved. Final idle:
**2026-11-12T08:21:17.0479483Z**. No rerun or Windows contact during archival.

## Original hashes

All **28 collected hashes** and **7 uploaded-input hashes** were verified before
copying. The large source tar was verified in place but omitted; no executable,
build tree or credentials copied. `original-byte-sha256.json` covers **32 copied
files**, excluding itself and this README.

Original manifests and CRLF/BOM bytes remain unchanged. Hashes refer to **original
pre-filter bytes, not Git blobs**; repository clean filters may rewrite paths.
Any parent-created post-filter Git-blob manifest must remain a separate domain.
