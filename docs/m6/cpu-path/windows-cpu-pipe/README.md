# Bounded Windows CPU pipe prerequisite

Internal `SharedCpuPipeWin` consumes an already connected asynchronous local
message-mode pipe. The broker must authenticate it and supply the stable peer
process handle; matching the OS-reported pipe peer PID is a binding check, NOT
standalone authentication. Timeout1..60000ms and optional retained cancellation
event apply to both read and write. Synchronous/byte-mode/non-wait pipes reject;
NtQueryInformationFile FileModeInformation verifies asynchronous mode. Pipe
inheritance is cleared and duplicated process/event handles are non-inheritable.

Pending I/O waits on completion, peer exit and caller cancellation. Timeout or
cancellation calls CancelIoEx and drains completion before destroying the
OVERLAPPED/buffer, then closes the pipe. Raw malformed framing also closes it.
Outputs remain unchanged on failed receive. This is separate from legacy native
Channel; no legacy timing/default change or Windows CPU public API is claimed.

Windows4/4 focused checks pass (pipe, allocator, imported pool, legacy framing).
Pipe test includes100 exact handle-clean bidirectional exchanges; async-only and
peer/timeout preconditions; silent-peer read and quota-stalled write timeout;
mid-read cancellation; disconnect; trailing-byte rejection with output unchanged.
Each timeout/cancel completes within a frozen1s wall bound; configured timeout30ms
and cancellation20ms. These are real named pipes in one process, not an end-to-end
authenticated cross-process CPU channel. Kernel scheduling is not a hard realtime
guarantee. Peer process handles need SYNCHRONIZE/query rights; future handle
transfer will additionally require authorized PROCESS_DUP_HANDLE rights.

Evidence: base609cb74 plus recorded changed-source hashes, built in the isolated
Windows tree. Prior candidate evidence remains pinned to its original revision;
the working tree was subsequently extended for this prerequisite.
Next: explicit authenticated Windows pool handle exchange and acknowledgment,
then ready/release/metadata/EOS integration. M6/API freeze still open.
