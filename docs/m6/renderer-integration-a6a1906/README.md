# Isolated renderer integration — collected 2026-11-12

Approved renderer9f74b9d integrated with exact FrameLinka50841a on <win-host>,
Windows11Home10.0.26300, RTX5090 driver32.0.16.1714.
No unrelated checkout changed. Source verification:1624renderer +7796FrameLink files.
Fresh configure/build passed; focused scheduler/clock/pacer/NV12 CTests **4/4**.

## Scoped repair

Before repair, explicit `--av-latency-ms 0` failed numeric validation (preserved
zero-cli-before evidence). Renderer commit **a6a19061360761729db41bfa427f364037985b1a**
accepts latency0..2000, rejects empty values and retains nonzero geometry/rate/timeout
requirements. Default100ms unchanged; no ring/pending, API, queue or fence changes.
Only renderer executable changed among the integrated binary pins. Nine CLI controls pass.
Exact two-file patch, source pins, final source/binary checks and dependency commits archived.

## Captures and identity checks

All are finite5s NV121920x1080p60 native-input renderer controls, real audio sink,
ring12/pending6, exact300source identities and240000PCM samples/300audio buffers.
Producer-clock policy retained. No end-to-end zero-copy claim: renderer explicitly
CPU-converts NV12. These checks qualify submission/control behavior, not endurance,
small-pool equivalence, physical display/acoustic timing or broader formats/rates.

- `legacy100`: original capture fails trace completeness (core dropped79429).
  Role exits/counts pass but **identity qualification FAILED**; evidence retained.
- `legacy100-complete`: corrected trace capacity; baseline100ms **PASS**.
- `explicit0`: repaired binary, negotiated0ns; **PASS**,11025 matched submission serials.
- `explicit100-fixed`: repaired binary, negotiated100000000ns; **PASS**.

`check_identity.py` verifies source/acquire/adopt/frame PTS and fence IDs, all300
identities, exact render/GPU-complete/present-submission serial sets and causal order,
no frame-drop events, audio counts, settings, role exits and all four zero-loss trace
footers. GPU completion is after fence/device completion; present submission is API
return, not scanout. Final process idle0 verified23:13:39Z.

## Integrity and reproduction

External full evidence:
`<linux-home>/dev/evidence/m6-renderer-integration/renderer9f74b9d-framelinka50841a`.
Remote isolated build:
`<win-home>/dev/m6-renderer9f74b9d-framelinka50841a-20261112`.
67remote result hashes verified with complete local coverage. Controller scripts,
commands, captured raw outputs, dependency and binary/source pins are included.
Repository local-environment clean filters normalize machine-local paths in text
blobs; original byte hashes refer to the external collection before that filter.
A separate Git-blob manifest pins the committed representation. Large traces are losslessly `.gz` compressed; `archive-manifest.json` records both
original and archived SHA256 so extraction must reproduce original bytes.
The unchanged full original traces remain external. No test rerun is needed to inspect.

M6 remains open. See [approved scope/checklist](../M6_WINDOWS_READINESS.md).
Unresolved BGRA1080/decoder4K and lifecycle failures are not repaired or waived here.
