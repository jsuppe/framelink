# Explicit shared-CPU control setup

Internal Linux/Android `SharedCpuControl` now connects the preflight and atomic
import components through the existing bounded AF_UNIX/SEQPACKET channel. It is
**not selected by any legacy channel factory** and has no fallback to DMA-BUF/AHB.
FL_VERSION, legacy Hello/RingDesc layout, session wire and default feature words
are unchanged. New message types 9/10 are used only by this explicit setup class.

Both participants send then receive a fixed 256-byte FCH1 offer containing an
opposite endpoint role, exact encoded session, one endpoint's memory requirements
and declared COPY operation. Reserved bytes, version/tag/size, unexpected handles,
session disagreement, same-role peers, GPU requirements and forbidden operations
reject and close the control connection. The complete session byte comparison
binds session/clock IDs, generation, tracks, formats, epoch, queues and timeouts.
The broker must authenticate/delegate this socket; these bytes are not credentials.
The coordinator must first validate session selection against actual peer offers.

Only after agreement can the consumer send one FCP1 pool: fixed 16-byte header,
2..16 exact FCS1 descriptors, and exactly one SCM_RIGHTS FD per slot. The producer
checks the envelope and descriptor count, then uses atomic rights/identity-checked
pool import. Received FDs are closed on every normal success/error path. Unexpected
legacy message types never trigger fallback. Setup cannot be repeated on the same
control instance; replacement needs a fresh explicitly negotiated connection and
existing imported-pool replacement validation.

`sendPool` now waits for an exact 24-byte FCA1 acceptance (message type 11):
version, generation, slot count and zero reserved bytes, with no ancillary FDs.
Only a matching reply sets `poolAccepted()`. Silence, close, wrong type, stale
identity, malformed bytes or unsolicited FDs fail closed within the socket's
configured I/O bound. A failed connection cannot be retried or used for media.
This is an authenticated peer's assertion, not proof against a dishonest peer.

The producer sends acceptance only after complete atomic import. If sending that
ACK fails, `receivePool` returns DISCONNECTED and the control is not accepted;
the output pool nevertheless retains the successful local import. Its mappings
remain safe owners, but the caller must not publish media on a failed setup.
ACK send success cannot guarantee the remote received it; readiness still needs
the forthcoming frame protocol. This is normal distributed-connection uncertainty,
not permission to reuse slots. Session-level retry must use a new generation and
respect old held storage.

**There is still no live frame protocol.** Next: ready/release ownership,
generation and sequence checks, and bounded peer-death behavior before public
factories or GStreamer expose this route. Native GPU entry points remain unavailable.

## Evidence

Linux 7/7 shared-CPU and connected-IPC tests pass. Android control and connected-IPC
runtime tests pass; arm64 and x86_64 builds pass. Real forked producer receives
allocations created only after fork, validates all 1024 bytes of each of two slots,
and writes back through received mappings; parent verifies the same backing bytes.
Negative peers cover session generation, role, strict copy permissions and GPU
usage. Eight malformed envelope/FD cases run ten times each with unchanged FD
count: unknown tag/version, reserved bytes, legacy type, short hello, unsolicited
hello FD, wrong pool FD count and invalid pool descriptors. Partial setup publishes
no imported pool. Existing connected-IPC test verifies bounded silent-peer receive.

Android deployment initially attempted to copy a nonexistent libframelink.so;
this build statically links libframelink.a. The binaries had copied successfully;
they were then run directly and passed. This was a controller deployment mistake,
not a runtime failure. Windows allocator/control implementation remains open.
Raw results and source hashes: `shared-cpu-control/`.

## Pool acceptance validation

Linux 7/7 shared-CPU/IPC tests and Android control runtime pass, with both Android
ABI builds. The real cross-process import now completes the ACK exchange. Nine
additional cases reject unknown tag/version, stale epoch, wrong count, reserved
bytes, legacy message type, short payload, unsolicited FD and silent peer. The
silent case uses a 30ms socket bound and must return within one second. FD counts
return to baseline after every case. Results: `shared-cpu-acceptance/`.
