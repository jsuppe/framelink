# Corrected CNG decoder oracle: build, smoke and 1080p60 pair

Exact source: `77afa1a4e9abcbdfdcae007d3d6dfb4dac97763f`.
These are successful bounded checks of the test-only CNG full-SHA256 oracle.
They are **not endurance, full milestone or physical-playback qualification**.

## Build and tiny smoke

[Build status](build-smoke/status.json) records successful configure/build and
**9/9 focused tests**, including SHA256 correctness checks. The [test log](build-smoke/LastTest.log)
retains individual outputs. Two-second tiny decoder smoke completed **60 distinct
frames / 96,000 PCM frames**, with zero arrival misses and 7.0902ms maximum video
age. Trace was off. Final idle: **08:09:55.4335642Z**.

## Matched decoded 1080p60 pair

Each arm uses actual 1920x1080 H264 decode to RGBA, independent 100Hz PCM, exact
full-pixel source/final SHA256, real native identity/lease/fence and terminal checks.

| Five-second arm | Distinct video frames | PCM frames | Video/audio 17ms misses | Video age max | Trace checked |
| --- | ---: | ---: | ---: | ---: | --- |
| Trace off | 300 | 240,000 | 0 / 0 | 10.1737ms | false |
| Trace on | 300 | 240,000 | 0 / 0 | 11.0694ms | true |

Both analyses report **`pass:true`** and **`trace_qualified:false`**. Trace-on checked
bounded raw loss/counts, native transport identities and relay payload-copy/map
invariants; it does not expose uninstrumented GStreamer/D3D work. The identical five
binary pins are preserved in [pair status](1080p60-pair/status.json) and agree with
the build. Final idle: **08:10:32.3916240Z**.

Raw CSVs, traces, exact configs, all role summaries, host receipts, controller and
remote status, and original analyzer outputs are retained in both subdirectories.
No duration run is included. Source conversion and source/final oracle readback
remain explicit workload costs; this is not a zero-copy end-to-end or renderer claim.

## Separate 4K30 preflight failure

The [initial 4K30 trace-off capture](4k30-initial-failure/README.md) **FAILED** the
unchanged 34ms video-arrival gate: 140/150 misses (identities 10–149), zero audio
misses, despite complete 150 distinct frames / 240,000 PCM frames. Maximum video
age was 654.186767ms. Trace-on and duration were **not run**. This failure is kept
separate from the successful 1080p pair above; no 4K or endurance pass is claimed.
Its raw files and original hash manifests are retained in that subdirectory,
with independent counts and provenance in its README.

## Provenance and hash domains

Original run directories:

- `windows-native-ready-batch-prepared/runs/cng-limit-fix-20261112t0810z`
- `windows-decoder-rate-target-prepared/runs/decoder-1080p60-cng-77afa1a-20261112t0810z`

Both are under the local `evidence/m6-cpu-path` root. Run-name minutes are labels;
use captured UTC receipts for actual time.

Verified before copying:

- Build: **32/32 collected hashes**, **5/5 uploaded-input hashes** (including the
  source tar, verified in place but deliberately not copied).
- Pair: **55/55 collected hashes**, **5/5 uploaded-input hashes**.
- Per-subdirectory `original-byte-sha256.json` covers **38 build files** and
  **57 pair files**, respectively, excluding the manifest itself and this README.

Original `artifact-sha256.json` and `collected-sha256.json` are unchanged. Sources,
logs and raw CSVs total about 2MB; no executable, source tar or discovery credential
is archived. Original CRLF/BOM bytes are preserved. These hashes describe original
pre-filter bytes, **not Git blobs**. The parent owns any separate post-filter
Git-blob manifest because repository clean filters may rewrite environment paths.
Archival involved no Windows access, build or execution.
