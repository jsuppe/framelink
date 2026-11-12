# Authenticated local three-process decoder fixture — 4d38a09

**PASS**, exact test revision `4d38a099c1790caa4be091bb5bbf3b9f8a13cf14`.
Production sources remain at `461584b`; this is a focused test-harness build,
not a new full-suite/API baseline.

Clean Windows configure/build, direct fixture execution and independent checker
all exited zero. CTest inventory confirmed the new test was registered once;
this capture ran its executable/checker directly, not the whole CTest suite.

## Verified scope

- Three actual processes, protected authenticated FAR4 source-relative input,
  consumer-pool snapshot and reversed-handle FAR5 output. No inherited textures.
- Sixteen distinct real H.264-decoded frames:320×240 NV12 input, explicit GPU
  conversion into issued64×32 RGBA reservations. Decoder storage remains held
  until actual conversion GPU completion.
- Source/final full-pixel digests match; actual decoded PTS/duration and absent
  DTS survive the relay.16 frames,25600 synthetic stereo PCM sample frames,
 102400 PCM bytes and37ms common mapped video/audio separation.
- Final reader retains a real native lease; relay remains pending until its
  release. The final image is checked again through the retained lease.
- Authenticated END/EOS and exact terminal counts pass. Snapshot lifetime remains
  separate from per-frame read ownership.

| Explicit work | Payload accounting |
| --- | ---: |
| Source converted output | 16×8192 =131072 bytes |
| Source oracle readback | 16×8192 =131072 bytes |
| Final oracle readback, including second final-frame check | 17×8192 =139264 bytes |

Conversion-output bytes describe output payload, not total GPU traffic. This is
**tiny functional validation**, with per-frame fixture controls—not target-rate,
resource endurance, physical presentation or end-to-end zero-copy qualification.
Only the native-video relay after explicit conversion avoids payload mapping;
source and final oracle copies are deliberately included above. Historical
startup/lifetime failures and broader M6 gates are not waived. M7 remains held.

## Provenance and reproduction

Host: Windows11 Home10.0.26300; Ryzen7 9800X3D (8 cores/16 logical);
RTX5090 driver32.0.16.1714. See [host and runtime records](data/status.json),
[CPU](data/cpu.json), [GPU](data/gpu.json), [OS](data/os.json), and
[idle verification](data/idle-final.json).

Remote work started07:30:37Z and finished07:31:00.946365Z on2026-11-12.
Runtime was independently observed empty at07:31:01.0481721Z.
The controller finished collecting at07:31:05.905644Z. This is historical release
evidence, not a claim that the Windows lane is currently idle.

The four built runtime binary hashes are in [status.json](data/status.json).
They were recorded by the runner before execution. Binaries were not copied
into this archive and were not independently rehashed by this archival step.
Exact source archive SHA256:
`094671410eb28104781304403cf5ce5e1f0e27275abcef9708b782019022cce6`.

[Source-pin verification](source-pin-verification.json) verifies all five
uploaded artifacts, including the external source archive. [Archive manifest](archive-sha256.json)
covers every archived file except itself. The archive-side checker independently
revalidated the captured CSVs: [result](archive-verified-analysis.json).

Scripts are retained as [controller](run.py), [exclusive-lane wrapper](execute.ps1),
[remote build/run commands](remote.py) and [report-only collector](collect.py).
The controller was captured from the prepared-script directory; remote scripts,
config and upload hashes came from the executed run directory. The pinned test
and checker sources are in `source-files/`. [Commands](commands.json) expand the
recorded remote recipe; they are documentation, not permission to rerun.

Original evidence remains at:
`<linux-home>/dev/evidence/m6-cpu-path/windows-local-decoder-prepared/runs/decoder-4d38a09-20261112t0730z`.
This archive deliberately excludes `source.tar`, build products, discovery
credentials/endpoints and the report tarball. Do not rerun already-collected
validation merely to reproduce this archive.

### Repository path normalization

`archive-sha256.json` records original local pre-filter file bytes. The repository clean filter normalizes machine paths; `git-blob-sha256.json` instead hashes exact committed bytes, excluding itself. Neither alters the original external evidence hashes or runtime binary pins.
