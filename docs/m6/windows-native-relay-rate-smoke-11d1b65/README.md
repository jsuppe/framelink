# Windows native local relay — corrected two-second functional smoke

**PASS: functional smoke only, not target-rate, endurance or lifecycle qualification.** Exact source revision `11d1b65a84029c3fc84509e3b024b984bc240424`, freshly configured and built on Windows. Production directories `src/`, `gst/`, `include/` and `samples/` are unchanged from `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`; the rebuilt DLL hashes are recorded, not assumed identical to earlier build directories.

The smoke uses BGRA 64×32 at 30fps for two seconds, pool2/unread1, video PTS offset37ms, and tracing disabled. The relay process starts separate source and receiver processes without inheriting handles. The source uploads native D3D11 frames; authenticated local session setup connects the actual three-process graph. The receiver uses real native read leases and fence completion; this is not a mocked handoff.

## Result

| Role | Video identities | PCM sample frames | Audio blocks | Terminal |
| --- | ---: | ---: | ---: | --- |
| Source | 60 | 96000 | 200 | true |
| Relay | 60 | 96000 | 200 | true |
| Receiver | 60 | 96000 | 200 | true |

The pinned analyzer was extracted from the exact source archive, compared byte-for-byte with the same git revision, and independently run against the collected `data/smoke` records. It passed ordered identities, PCM sizes/cadence, exact metadata/timelines, per-role A/V mapping, cross-hop source stamps, causal phase ordering and receiver lease/completion accounting. The source deliberately reports `held_last_proof:false`; the relay and receiver both report `true`.

The final-reader control retains the last native lease while the relay collects twice and confirms an outstanding reader. Only after the release signal does the receiver re-query its retained lease and genuinely complete it; the relay then drains and finishes. This demonstrates the bounded held-reader fixture, not repeated device-lifetime/resource return or survival across every possible graph teardown.

The short run had zero whole-path34ms video/audio diagnostic misses: scheduled-to-final video max4.352ms, audio max0.5998ms. These are **smoke observations**, not target-resolution rate acceptance or an approved isolated-hop latency budget. Resource samples are retained, but two seconds does not qualify resource slopes. The run was untraced; no independent traced zero-copy-operation claim is made. Source upload and receiver verification copies are explicit.

## Build, host and provenance

Configure/build/smoke, controller, packaging and collection all exited0. CTest inventory confirms the intended smoke is registered exactly once; this archive does not claim a full CTest suite run. The actual smoke invocation and all four runtime binary hashes are in `remote-final-status.json`; source/archive/analyzer/harness/CMake pins are in `source-build-binary-pins.json`. Build and configure logs are retained.

Host: Windows11 Home build26300, Ryzen7 9800X3D (8 cores/16 logical), RTX5090 driver32.0.16.1714; physical-memory bytes and individual host reports are included. Runtime idleness was verified at `2026-11-12T07:28:03.1988066Z`. The human-readable run ID ends in `0731z`; authoritative execution times are those recorded in the status files (07:27–07:28Z), not the label.

Original evidence:

`<linux-home>/dev/evidence/m6-cpu-path/windows-native-relay-rate-prepared/runs/native-smoke-11d1b65-20261112t0731z`

All five uploaded-artifact hashes were checked, including the source archive. All29 collected evidence-tar members were compared byte-for-byte with the local collected files. `evidence-verification.json` retains these hashes and the original evidence-tar hash. This compact archive includes the small raw CSVs, summaries, stderr logs and independent analysis; it excludes tar files, the source tree, build products and discovery credentials. `archive-sha256.json` hashes the archived files.

Earlier smoke attempts are separate evidence and are not overwritten by this corrected result. Target-rate preflight, sustained/native lifecycle qualification and full M6 acceptance remain separate. No physical-playback or M7 release claim.

### Repository path normalization

`archive-sha256.json` preserves the original local, pre-commit archive hashes. The repository clean filter normalizes machine-specific paths, so those hashes are not hashes of normalized Git blobs. `git-blob-sha256.json` separately hashes the exact committed file bytes (excluding itself). Original external evidence hashes and runtime binary pins remain unchanged.
