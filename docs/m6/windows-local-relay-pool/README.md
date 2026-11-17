# Explicit local native relay pool composition

Implementation follows c4f164a. Exact per-phase source hashes accompany the logs. This is Windows-only opt-in setup; no legacy defaults changed.

- FAR5 protected discovery is distinct from FAR1/FAR3/FAR4. Old profile readers reject it. Mixed profiles cannot publish. The existing independent-pool native_relay API remains unchanged.
- New C entrypoints `fl_gst_session_open_local_native_relay_pool` and `fl_gst_session_open_local_native_pool_receiver` authenticate media/policy/clock/audio/events and a separate kernel-pinned native pool pipe. The producer retains exact upstream media, clock, epoch, generation and tracks with a fresh downstream identity.
- The caller borrows and pins all unpublished upstream allocation handles until setup returns. The receiver pulls duplicates from the authenticated producer process, verifies pool shape and imports those same allocations. Caller must bind the read-lease relay before source writes; this API is not permission for independent writers.
- Native D3D allocation and legacy producer opening are synchronous. The setup cancellation signal interrupts pipe/stream I/O, not arbitrary driver calls. The separate bounded native handshake work is not implied by these tests.

## Focused evidence

Setup Windows 3/3: C11 header/DLL linkage, typed discovery and local pool setup (1.24s). Live-session composition Windows 3/3 (2.41s): same-allocation native bin fixture retains the actual discovered sessions through eight original video frames and 384 PCM frames, exact 37ms A/V offset, negotiated final mapping, automatic supervisor, and genuine final reader completion before authenticated drain. Existing broker native-bin and serialized-event controls pass. Shared Linux 3/3 (1.14s): rendezvous and session transport/context; no broad campaign.

Real process boundary 1/1 (0.88s): final receiver runs in a child process. Only three control-event handles are inherited, not textures. FAR5 transfers original textures via authenticated pinned-peer pull duplication. Eight independently checked pixel/PTS/DTS/duration/generation frames pass; the held final native reader prevents upstream lease completion until explicitly released; authenticated drain follows actual completion. This raw process control does not claim cross-process PCM/bin end-to-end qualification.

Corrected malformed-import, cross-process and live-composition checks pass 3/3 (2.60s): invalid/duplicate/nontexture handles, unavailable LUID and capacity mismatch reject at the pool-transfer stage; legacy receivers reject FAR5. All119 prior exports are retained;122 total include the existing relay prerequisite plus two new pool entrypoints. This is exported-name compatibility, not full ABI/freeze qualification. API is not frozen. Remaining general application composition, reconnect/dead-generation and original Windows timing/lifetime acceptance work remains in the authoritative ledger. No full suite, endurance or broad Linux/Android refresh was run here. M6 open; M7 hold.

## Retained negative evidence

The initial malformed-import fixture failed. An instrumented follow-up returned exit0 but all handle cases stopped at clock setup (epoch0 could map below zero); this is explicitly NOT import coverage. Correcting the fixture to a current positive epoch and requiring exact pool-transfer-stage errors isolated the production defect: invalid all-FF adapter LUID returned success on both endpoints, while duplicate/invalid/nontexture handles correctly rejected. See luid-reproduction/LastTest.log.

The native relay transfer now queries and verifies the actual imported ring LUID/geometry/capacity before readiness/attach ACK, refusing any backend default-adapter substitution. Generic backend createDevice fallback remains separately assigned; this boundary check does not claim to repair every native entrypoint.
