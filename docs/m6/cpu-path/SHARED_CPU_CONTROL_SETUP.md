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

A successful send means only that the kernel accepted the setup packet, not that
the peer accepted the import. **There is no pool acknowledgment or live frame
protocol yet.** Do not recycle slots or claim frame delivery from this setup. The
next integration step must add pool acceptance/ready-release ownership, generation
and sequence checks, and bounded peer-death behavior before public factories or
GStreamer expose this route. Native GPU entry points must remain unavailable.

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
