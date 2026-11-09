# Windows broker-bound CPU pool setup

Internal SharedCpuControlWin now negotiates canonical FCH1 session/role and both
CPU-only memory policies over SharedCpuPipeWin. Explicit FCW1 offers contain
128-byte descriptors plus 64-bit **peer-process** handle values. The producer
pulls SAME_ACCESS duplicates from the pipe's pinned peer process, validates actual
section type/size/rights, rejects kernel-object aliases and imports atomically.
Only an exact FCA1 generation/count ACK marks setup accepted. No remote handle is
closed, and no wire value is ever used as a local handle. The consumer must retain
its exports until ACK or setup failure. Receiver failures clean partial imports.

This is a **broker-authenticated transport prerequisite**, not authentication by
PID. The caller must already authenticate/delegate the exact local pipe and peer
process handle with DUP_HANDLE/query/synchronize rights. Public discovery,
coordinator delegation, authentication binding and CPU channel integration remain
pending. Per-I/O deadlines are inherited (up to four bounded I/Os for full setup);
no whole-setup single-deadline guarantee is claimed. Metadata/EOS features and
ready/release are not yet implemented in this setup-only revision. No GPU fallback.
No public ABI/default changes, no Windows performance or M6 completion claim.

Validation: Windows Release **5/5** selected tests passed, including the new
control test: 12 real child-process BGRA/NV12 pools, all active pixels checked;
57 negative session/role/features/operations, pool type/reserved/length,
null/pseudo/non-section/RO/aliased handles, and wrong/stale/malformed/missing ACK
cases. Exact handle baseline is checked after each case. Existing pipe,
allocation, atomic pool and IPC framing regressions also pass. SHA-256 hashes in
provenance.json bind the staged files on the b997e7c Windows source tree.
Initial build failed due a missing method signature; the next due a test enum
narrowing conversion. Both logs retained; fixed before the passing build.
Loom pre-edit lookup unavailable (`loom: command not found`); no traceability claim.
