# Windows native relay BGRA1080p60 — 1800-second qualification

**PASS for this synthetic native transport cell**, using the four binaries built at `11d1b65a84029c3fc84509e3b024b984bc240424`. One1800-second trace-off run; no retry, rebuild, repeated preflight or other media workload. The runner first revalidated the already-collected matched BGRA1080 trace-off/on pair locally. Its earlier collection-path failure remains preserved in the [target preflight archive](../windows-native-relay-targets-11d1b65/README.md).

The [exact build/smoke archive](../windows-native-relay-rate-smoke-11d1b65/README.md) records source provenance and fresh build hashes. Production directories are unchanged from461584b. Four binary hashes match before/after this duration run and its preceding pair.

## Profile and complete accounting

BGRA1920×1080@60,1800seconds, actual source/relay/final-receiver processes, authenticated local native pool setup, pool2/unread1, serialized PCM and video PTS offset37ms. Tracing is off. The source explicitly uploads generated pixels, and the receiver explicitly reads back and verifies pixels. This is not an end-to-end zero-copy or real-decoder workload.

| Role | Video frames | Contiguous audio blocks | PCM sample frames | Terminal |
| --- | ---: | ---: | ---: | --- |
| Source | 108000 | 180000 | 86400000 | true |
| Relay | 108000 | 180000 | 86400000 | true |
| Receiver | 108000 | 180000 | 86400000 | true |

The pinned analyzer verifies full ordered identities, metadata/timelines, PCM sizes/cadence, per-role common A/V mapping, cross-hop source stamps and causal phases, and actual receiver lease/completion accounting. Relay and receiver report final-held-reader proof: the relay observes an outstanding reader until the receiver is signaled to complete its actual native lease. The source's `held_last_proof:false` is expected; it is not the reader.

## Timing and resources

All recorded frames, including startup and drain, pass the existing whole-path17ms arrival diagnostic. Video scheduled-to-final arrival p95 is5.933967ms, maximum8.647134ms; PCM scheduled-to-final maximum4.52ms. Video/audio misses are0/0. These are not approved isolated-hop latency thresholds.

Resource coverage and the original second-half steady-state regression gates pass:

| Process | Fitted private-memory KB/min | Handles/min | Threads/min | Measured CPU |
| --- | ---: | ---: | ---: | ---: |
| Source | 0 | 0 | 0 | 30.844716% |
| Relay | 0.400218 | 0.000572 | 0 | 13.294136% |
| Receiver | 0 | 0.000650 | 0 | 32.921805% |
| Limits | 64 | 1 | 0.5 | No additional cap |

A zero fitted slope is an observation for this window, not proof of zero allocation or absence of every leak. CPU and detailed phase distributions are retained in `duration-local-analysis.log`; receiver verification and source upload costs are included in the workload.

Remote execution, packaging, download and independent local analysis exited0. The original runner start receipt is07:31:21.091821Z on2026-11-12; remote analysis finished08:01:29.809569Z; final Windows idle receipt is08:01:29.9120437Z with no active runtime processes. Ownership was then released. Host CPU/RAM/OS/GPU reports are retained.

## Evidence and hash domains

Original evidence:

`<linux-home>/dev/evidence/m6-cpu-path/windows-native-relay-duration-prepared/runs/native-bgra1080-1800-11d1b65-20261112t0732z`

All **32 collected manifest entries** and five uploaded-artifact hashes were independently reverified locally, including **nine raw CSVs totaling117756087bytes**. Those32 entries are not32 CSV files. See `original-evidence-verification.json` and `final-verification.json`. The exact analyzer was also byte-compared with git11d1b65. The parent independently reanalyzed the raw result and confirmed PASS.

This compact archive contains commands, scripts, configuration, original statuses, host/idle receipts, summaries, analyses and manifests. Large raw CSVs, tar files, discovery credentials, sources and binaries remain outside the repository archive.

Hash domains are deliberately separate:

- `collected-sha256.json` and `original-evidence-verification.json` refer to original collected bytes and paths under the external evidence root. `artifact-sha256.json` refers to the uploaded runner artifacts.
- `archive-sha256.json` records this compact archive's **original copied working-tree bytes before Git staging/line-ending normalization**. It does not claim to hash normalized Git blobs and excludes itself.
- `git-blob-sha256.json` separately records the normalized **Git-blob** hashes. Git text normalization can change CRLF-bearing file hashes without changing the original evidence; do not overwrite the original-byte manifest to conceal that distinction.

This qualifies only the stated synthetic native BGRA1080p60 duration cell's accounting, whole-path arrival diagnostic and resource gates. It does not qualify other native formats/rates, decoder performance, physical presentation, an isolated-hop budget, repeated device lifecycle or all of M6. API not frozen; M7 held.
