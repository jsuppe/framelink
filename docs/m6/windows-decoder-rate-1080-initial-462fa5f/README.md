# Initial decoded 1080p60 preflight — timing failure

Exact source `462fa5f192cd9b3347c798afa8108b9358716f7b`.
Source run: `windows-decoder-rate-target-prepared/runs/decoder-1080p60-462fa5f-20261112t0806z`.
This is the initial five-second **trace-off** preflight, not a successful matched pair.

- Executable completed successfully; independent analyzer **FAIL** (exit 1).
- **300 distinct full-frame SHA256 identities** and **240,000 PCM frames** completed;
  all three roles have exact terminal accounting.
- **300/300 video arrivals missed the unchanged 17ms gate**; zero audio misses.
- Scheduled video age: **p95 1178.863ms**, **maximum 1252.220ms**.
- Source oracle readback+hash: **p50 18.664ms**. Final staging map+hash:
  **p50 18.331ms**. These are combined measured phases, not isolated hash costs.
- Trace-on **NOT RUN** after failed trace-off analysis; duration **NOT RUN**.
  `trace_checked:false`, `trace_qualified:false`.
- Five binary hashes unchanged before/after. Final idle at
  **2026-11-12T08:06:11.901Z** (full precision in [receipt](idle-final.json)).

[Analysis](trace-off-analysis.log), [status](status.json),
[raw profile](trace-off/config.json), all role video/audio/resource CSVs and
summaries are preserved. The 435,514-byte collected payload is below the 2MiB
archive limit, so no raw CSV is omitted. Source conversion, source readback and
final readback each accounted for **2,488,320,000 bytes**.

This measures the complete declared decoder/conversion/oracle workload. It is
not proof of an isolated transport regression, and the oracle phase timings do
not yet identify how much time was spent hashing versus mapping/readback.
At archival handoff, root was preparing a Windows CNG-versus-GLib equivalent
full-SHA256 comparison to investigate oracle cost. **No fix or improved result is
claimed here.** There is no trace-pair, sustained-rate or physical-playback pass.

## Provenance and hashes

Original raw files, exact uploaded scripts/analyzer, configuration, host details,
status, binary pins and idle receipt are retained. No source tar, executable or
discovery credentials are copied. The retained scripts are evidence, not a request
to execute them; this archive was created without Windows contact.

- [artifact-sha256.json](artifact-sha256.json): original uploaded-input hashes.
- [collected-sha256.json](collected-sha256.json): original Windows manifest;
  all **32 entries verified** before copying.
- [original-byte-sha256.json](original-byte-sha256.json): **34 copied files**,
  including collection manifest and controller status, excluding itself and README.

These preserve original bytes (including CRLF/BOM) and describe **pre-filter byte
hashes, not Git blobs**. Repository clean filters can rewrite environment paths at
staging. The parent may add a separate post-filter Git-blob manifest; original
manifests must remain unchanged and in their original hash domain.
