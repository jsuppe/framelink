# Explicit CPU channel delegation to the GStreamer coordinator

`fl_gst_session_bind_shared_cpu_fd` is a Linux/Android worker-thread seam for an
**already broker-authorized connected SEQPACKET FD**, not an authentication API.
The caller must authenticate the session offers and authorize the delegated socket
before calling. Existing authenticated TCP/local-native setup is unchanged and
never silently selects CPU. No Android broker implementation is claimed here.

The function consumes the FD on every path, snapshots the configured generation,
reserves a single setup slot, and runs strict CPU negotiation with both metadata
and ordered EOS required. Consumer descriptors and both real memory policies still
pass the underlying pool/FD/identity checks. Successful setup is published only
if the exact generation remains current and uncancelled; configure already
requires strictly newer generations. It does not mark video ready or install a
clock mapping. Audio-only, stale-generation, duplicate binding and malformed
parameters reject. Failed transport setup returns INVALID; cancellation/staleness
retain their distinct coordinator results. Per-I/O timeout bounds remain explicit.

`fl_gst_session_claim_shared_cpu` transfers the owned opaque channel exactly once
for the correct role and generation. Until claim, the session owns the channel.
After claim, the session retains only a CLOEXEC duplicate socket used for shutdown,
not a channel pointer or lease ownership. Cancel/reconfigure/finalize interrupts
I/O without racing/freeing the externally serialized channel or retained frames.
The setup-in-progress reservation remains until the old worker returns, preventing
its completion from clearing a newer setup. Caller holds the session reference
through setup and externally serializes channel/lease calls afterward.

Ownership review found that a shutdown duplicate could otherwise keep a claimed
channel's peer connected after flSharedCpuClose. SharedCpuControl now explicitly
shuts down its socket on destruction/failure before closing. This preserves the
public immediate-disconnect contract even while a coordinator is still alive.
No generic/native Channel close behavior was changed.

## Evidence

Linux19/19 affected CPU/contract/coordinator tests pass. Android eight affected
runtime binaries pass; arm64 and x86_64 builds pass. The new coordinator test
checks exact role/generation, one claim, rejected duplicate bind consuming its FD,
no premature track readiness, metadata and EOS, valid held pixels after cancel
and replacement, unclaimed cleanup, and peer disconnect on owner close while the
session still owns its cancellation duplicate. Silent-peer setup is observed
sending its hello before cancel/reconfigure; the blocked60-second-I/O operation
returns within the frozen1-second cancellation gate. Every case returns to the
exact FD baseline. Existing authenticated session transport tests still pass.
Commands, logs and source hashes: `shared-cpu-coordinator/`.

This is only the typed coordinator/broker delegation seam. Video-element CPU
adoption/mapping, A/V-bin selection, actual broker/local rendezvous provisioning,
full A/V and target-rate/endurance qualification remain pending. Windows shared
CPU allocation is still unavailable. No new public fallback/default is inferred.
