# Windows native relay — matched target-resolution preflights

**Four five-second trace-off/trace-on pairs pass independent analysis. These are preflights, not sustained-rate or lifetime qualification.** All use the same four pinned native-smoke binaries built at `11d1b65a84029c3fc84509e3b024b984bc240424`. The production directories are unchanged from `461584b`; see the [exact build/smoke archive](../windows-native-relay-rate-smoke-11d1b65/README.md). The shared-CPU sustained results are separate profiles and do not qualify this native graph.

Each run has actual source, relay and final-receiver processes, authenticated local session setup, a two-slot native pool, unread queue1, serialized PCM, and a37ms video PTS offset. Each arm is five seconds, with the trace-on arm started only after trace-off execution and pinned analysis succeeded. No media repeats were used to recover collection.

## Completed pairs

Counts below are **per arm and per role**. Every arm has500 contiguous audio blocks/240000PCM sample frames, terminal accounting and the bounded final-held-reader proof. Whole-path video/audio arrival diagnostic misses are zero throughout.

| Cell | Video count | Existing diagnostic | Video maximum off/on | Audio maximum off/on |
| --- | ---: | ---: | ---: | ---: |
| BGRA1920×1080@60 | 300 | 17ms | 4.998134 / 7.053334ms | 0.7996 / 0.7442ms |
| NV121920×1080@60 | 300 | 17ms | 4.5713 / 5.352967ms | 0.6287 / 1.1763ms |
| BGRA3840×2160@30 | 150 | 34ms | 8.8069 / 11.314067ms | 2.199 / 1.0007ms |
| NV123840×2160@30 | 150 | 34ms | 6.2235 / 7.6759ms | 0.739 / 0.8854ms |

The trace-on analyzer verifies zero dropped/rejected trace records, source N sends, relay N receives/N sends, final N receives, and matching identities across two channel boundaries. Recorded relay operations show no payload map/upload/readback or positive payload-copy byte count. These checks do **not** make the whole harness zero-copy: source pattern generation/upload and final receiver readback/full-pixel verification are explicit. The recorded 17/34ms limits are existing **whole-path arrival diagnostics**, not approved isolated-hop budgets. Five seconds is insufficient for resource-growth or sustained-rate acceptance; no such gate is claimed here.

## Original BGRA1080 collection failure retained

`bgra1080/controller-status.json` intentionally remains `state:failed`: remote execution, packaging and download succeeded, but local verification treated a Windows backslash manifest path as a literal Linux filename. `recovery-receipt.json` records separator normalization,55 verified hashes, successful analysis of both existing captures and the original idle receipt. There was no Windows media rerun. The recovered preflight result does not rewrite the original controller failure. Other three controller receipts are finished/successful.

The per-case directories retain the exact uploaded runner/analyzer scripts, configuration and binary pins, remote/controller status, host/idle receipts, raw manifests, original analyses and freshly regenerated independent analyses. `commands.json` reconstructs each media argument vector from its exact archived `remote.py` and configuration, with path separator spelling normalized. The top-level `controller-run.py` is the available controller after the local manifest-path repair, not a claim to preserve the original failing controller source byte-for-byte.

## Verification and external raw evidence

All220 collected-manifest entries were rehashed successfully, including84 raw/trace/resource CSVs totaling5222034bytes. Each case's `raw-hash-verification.json` records its55 matches, upload verification and external paths. The analyzer in each case was byte-compared against `tools/check_local_native_bench.py` at exact11d1b65, then rerun locally on both original raw capture directories. Binary pins match before/after both arms; final idle receipts are retained. No Windows command was run to prepare this archive.

Raw captures and traces remain under:

`<linux-home>/dev/evidence/m6-cpu-path/windows-native-relay-target-prepared/runs/`

Exact run directories:

- `native-bgra1080-11d1b65-20261112t0729z`
- `native-nv121080-11d1b65-20261112t0730z`
- `native-bgra4k-11d1b65-20261112t0730z`
- `native-nv124k-11d1b65-20261112t0731z`

`collected-sha256.json` paths are relative to each original capture root, not this compact archive. CSVs, tar files, binaries, source trees and discovery credentials are not copied here. `archive-sha256.json` independently hashes the files actually archived. Run-name time suffixes are labels; use receipt timestamps for actual execution order/times.

This evidence does not establish decoder integration, physical playback, long-run native resource/lifetime acceptance, an isolated-hop latency contract or M6 completion. API not frozen; M7 held.

### Repository path normalization

`archive-sha256.json` preserves the original local, pre-commit archive hashes. The repository clean filter normalizes machine-specific paths, so those hashes are not hashes of normalized Git blobs. `git-blob-sha256.json` separately hashes the exact committed file bytes (excluding itself). Original external evidence hashes and runtime binary pins remain unchanged.
