# Authenticated local drain versus downstream cancellation

The exact f5b3ee3 integrated Windows build and API inspection completed, but
its serial suite failed **1 of 140**: `gst_av_bin` reached normal EOS and exact
30 video / 24000 PCM counts, then reported generation0 / empty session identity.
The producer observed event-socket closure caused by the already-drained consumer
teardown as a new cancellation. Preserve this failure; ccd0460 was not rerun.
`pre-fix/` contains the original outcome and failing full-suite output.
119/119 Windows exported names were retained in f5b3ee3; this does not make its
behavior an accepted API baseline. The pinned candidate remains ccd0460.

## Repair and acceptance boundary

A private, non-exported local-finished state is entered only after the existing
authenticated `D` drain handshake succeeds. It retires event liveness checking
and blocks further event operations while retaining negotiated session metadata.
It does not release native leases, synthesize video END or audio EOS, clear an
explicit cancellation, or change public structures, wire messages or defaults.
Reconfiguration resets the state. Failed or absent drain never enters it.
The state is private to the shared session implementation (hidden on ELF and
not exported on Windows); it adds no public ABI entrypoint.

The existing automatic-bin test joins the consumer (including teardown) before
checking session identity, generation and selected track IDs. It regressed on
f5b3ee3 and passes with this fix. A new event-channel negative sends/receives
video END, then cancels **without** a drain handshake: the idle producer must
still detect cancellation within1s and reject further publication. Existing
independent-process raw/GstMemory cancellation and actual producer death controls
remain required; normal drain is not permission to swallow premature closure.

## Validation

Focused Windows build/tests on the hashed working tree: **12/12 passed**,62.93s,
including automatic bin, local AV, event-channel, session context/transport,
native producer and all relay controls. The hidden-visibility annotation was
added afterwards (no MSVC behavior change); final clean exact-revision validation
is still required. Linux **4/4** focused checks passed (including the automatic bin). Android
**3/3** runtime controls and **arm64/x86_64** builds passed. Results are recorded
in adjacent files. No broad deferred platform campaign is claimed.

Raw evidence and commands:
`<linux-home>/dev/evidence/m6-cpu-path/windows-local-drain-f5b3ee3/`.
Original full-run evidence:
`<linux-home>/dev/evidence/m6-cpu-path/windows-integrated-f5b3ee3/`.

API not frozen; M6 open; M7 hold. No timing/lifetime acceptance failure is waived.
