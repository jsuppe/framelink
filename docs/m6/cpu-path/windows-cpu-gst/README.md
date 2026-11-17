# Windows broker-bound CPU GStreamer elements

The existing opt-in `shared-cpu=true` copy-in and copy-out implementations now
use the portable CPU channel on Windows. There is no new property/default, wire
change, GPU fallback or native-handle reinterpretation. The coordinator must
already hold a caller-authenticated, delegated local message pipe and pinned
peer process. This does not implement Windows discovery or qualify A/V bins.

## Validation

- Windows final **5/5**: CPU source, CPU roundtrip, D3D11 native producer,
  native source and copy-negative. Earlier same-production-code **5/5** other
  regressions passed: coordinator, session context/transport, native A/V bin,
  authenticated events. Initial two CPU fixture failures retained, not waived.
- Linux **4/4** and Android **4/4** source, roundtrip, events, A/V checks pass;
  both Android ABI builds pass. Only affected shared-code regressions run.
- Source eight cases: padded BGRA/NV12 exact pixels and planes; mapped PTS/DTS,
  duration/discontinuity, retained cache/fallback outputs through teardown,
  actual COPY permission, missing clock mapping, wrong generation, peer death,
  empty EOS, bounded cancellation of a60s idle wait.
- Roundtrip eight cases: BGRA/NV12, exact timestamps/pixels, COPY refusal,
  incompatible caps, invalid duration, multi-memory refusal, empty EOS and
  held-final-frame drain/cancellation. Events use an authenticated loopback
  stream on Windows; CPU frames use the delegated message pipe, not TCP.

Windows test ports initially exposed enum narrowing, missing platform header and
video-library linkage, then non-page-rounded descriptors. The allocator's exact
Windows section-size contract remains intact: fixtures now page-round allocation
size while retaining padded/noncontiguous active planes. All initial logs retained.
No production policy was weakened to make the tests pass. Provenance hashes in
final-provenance.json identify staged files (based on02509f7), not the exact02509f7
binary archive. The subsequent clean exact-commit run supplies integrated proof.

Loom unavailable on PATH; current ledger and source manually reviewed. Linux BGRA
sustained result was already archived and not repeated. Remaining Windows
A/V/bins/discovery, native producer coordination, timing, memory lifetime and
acceptance budgets remain open. API not frozen. M6 open; hold before M7.
