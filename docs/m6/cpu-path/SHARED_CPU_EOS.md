# Ordered CPU channel EOS

Both endpoints must select `FL_SHARED_CPU_ORDERED_EOS` (independent of metadata).
FCH1 flags at offset16 are now exactly matched combinations of wire bits2/4.
Older peers reject the new mode rather than silently weakening EOS semantics.
Legacy factories and ordinary receive behavior remain unchanged.

`flSharedCpuSendEos` sends type15/FCE1, a32-byte LE record containing magic,
version1, allocation generation, last submitted sequence (zero for empty streams),
and eight reserved zero bytes. It follows all submitted frames on the same socket.
Reserved writes return BUSY without sending; caller must submit/cancel first.
After sending, no new write leases are available; duplicate sends reject. This
is not a release acknowledgement: outstanding reads remain valid and must still
be released and drained normally. The producer may close after draining them.

EOS-mode callers use `flSharedCpuReceiveEvent`. Frames set eos=0; the exact
terminal marker sets eos=1 with all frame outputs untouched. Repeated calls return
the latched terminal event without blocking. Old Receive rejects EOS-mode use
without consuming a packet. Peer death/timeout never synthesizes EOS. Invalid
magic/version/generation/last-sequence/reserved bytes/length/FDs fail closed.
Packets after accepted EOS are not consumed: the stream is permanently terminal;
restarting media requires a fresh negotiated channel. This is not in-place reset.

Linux16/16 affected tests pass. Android six affected runtime binaries pass,
including100-generation reconnect; both Android ABI builds pass. EOS tests cover
empty streams, two ordered frames held across EOS, reverse-order final releases,
BUSY while a write is reserved, no reuse after EOS, combined metadata/EOS mode,
peer death and seven hostile packets. Every EOS scenario returns to its exact FD
baseline. EOS capability mismatch rejects in the control test. Initial test-only
missing POSIX includes were corrected (Android compiler failure retained).
Evidence/commands/source hashes: `shared-cpu-eos/`.

This is the channel prerequisite, not coordinator/GStreamer/A-V EOS integration
or target-rate qualification. Windows allocator and all remaining M6 gates remain
open. Loom pre-edit briefing was unavailable (missing chromadb); no requirements
were inferred from an unavailable store.
