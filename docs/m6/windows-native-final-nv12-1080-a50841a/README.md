# Final-candidate synthetic native NV12 1080p60 short pair — PASS

Exact source **`a50841aad44fbc9431234a4f326bad036d193bf5`**, using the fresh
[integrated build](../windows-integrated-a50841a/README.md).
Original run: `windows-native-final-target-prepared/runs/native-nv12-1080-a50841a-20261112t0837z`.

Both five-second arms delivered **300 video frames / 240,000 PCM frames** with
all independent identities, native completion/lease, held-reader, terminal and
A/V checks passing. **Zero 17ms video/audio arrival misses** in either arm.

| Arm | Maximum video arrival age | Trace checked |
| --- | ---: | --- |
| Trace off | 4.7345ms | false |
| Trace on | 5.322534ms | true |

Trace-on joins both transport boundaries with exact role counts and zero trace
loss, preserving no-relay-payload-copy checks. Raw role CSVs, trace, resource
series and local independent analyses are retained. Final idle:
**2026-11-12T08:36:26.9572574Z**.

## Binary provenance and scope

Runtime DLL pins match the fresh a50841a integrated build. The two executable
pins were **first observed after that build under the exclusive runtime lane**;
they are not retroactive proof of recorded build-time executable hashes. Exact
pin/provenance receipts and before/after checks are preserved under `data/`.

This is a synthetic native/PCM **short-pair** result, not a duration result,
real-decoder acceptance, physical playback, API freeze or full M6 closure.
It supplies the bounded prerequisite for this exact profile's later duration
validation; no such duration is claimed here. Other failures/suspensions remain.

## Original bytes and Git blobs

All **64 collected hashes** verified with complete file coverage, plus **12
uploaded-input hashes** verified. Exact scripts/configuration, prior validation,
source/build references, binary pins, host, statuses and raw evidence retained.
No executable or credential copied; empty owner-lock files, if present, are raw
artifacts rather than evidence of active ownership.

`original-byte-sha256.json` covers **68 copied files**, excluding itself and this
README. Original manifests and CRLF/BOM bytes remain unchanged.
`git-blob-sha256.json` separately hashes clean-filtered archive blobs, excluding
itself; original-byte hashes must not be treated as Git-blob hashes.
No Windows contact or execution occurred during archival.
