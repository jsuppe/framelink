# Windows SHA256 oracle comparison — CPU-only feasibility

Source run: `windows-decoder-hash-compare-prepared/runs/hash-compare-20261112a`.
Captured 2026-11-12 about 08:07 UTC. This is an isolated warm CPU-buffer experiment,
not a media pipeline run or target-rate qualification.

- Configure/build/benchmark/independent Python analysis **PASS**.
- **120 timing records** cover 12 iterations for each supported geometry/layout/
  method combination. Every recorded full SHA256 equals the independently generated
  Python digest. No truncated digest or sampled-pixel substitution is used.
- Known vectors, padding exclusion and reusable-hash behavior checks passed.
- Exact benchmark binary pin is unchanged before/after in [status.json](status.json).
- Final idle: **08:07:38.337Z**, full precision in [idle-final.json](idle-final.json).

| Packed RGBA geometry | GLib row-wise SHA256 p50 | CNG reusable row-wise SHA256 p50 |
| --- | ---: | ---: |
| 1920x1080 | 18.05375ms | 3.2428ms |
| 3840x2160 | 72.681ms | 12.95465ms |

[Raw timings](result/timings.csv), [summary](result/summary.csv),
[benchmark self-check result](result/result.json),
[Windows analysis](analysis.log), and [independent local analysis](local-analysis.log)
also preserve padded-row and packed-single-call results.

CNG provider/hash setup is **amortized outside the measured reusable-hash calls**;
these figures do not measure fresh-provider startup for every image. Buffers are
warm CPU data. The comparison excludes decoding, GPU conversion, staging/readback,
transport, real video pacing and physical rendering. It supports investigating the
oracle cost in the [failed 1080p preflight](../windows-decoder-rate-1080-initial-462fa5f/README.md),
not claiming that a media-rate failure is already repaired. At archival handoff,
a clean test-only CNG helper was being implemented; **its media validation had not
occurred**. No production API or qualification threshold changes are implied.

## Reproducibility and original-byte domains

The exact C++ source, CMake file, Python checker/controller/collector, PowerShell
wrapper, remote script, invocation, config, host details and logs are retained.
No executable, source tar, or credentials are copied. Archival performed no Windows
contact and no benchmark rerun.

- [artifact-sha256.json](artifact-sha256.json): all **7 uploaded-input hashes**
  independently verified before copying; their files are retained here.
- [collected-sha256.json](collected-sha256.json): original Windows manifest;
  all **17 collected entries** independently verified before copying.
- [original-byte-sha256.json](original-byte-sha256.json): all **28 copied files**,
  excluding itself and this README, preserving original CRLF/BOM bytes.

Original-byte hashes are **not Git-blob hashes**. Repository clean filters may
rewrite environment paths when staging. The parent owns a separate post-filter
Git-blob manifest if needed; preserve these original manifests unchanged.
