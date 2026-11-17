# Fine-grained Android CPU stage tracing

Instrumentation atop f2aba61, source hashes in ../android-stage-instrumentation/status.json. Linux affected checks and nine Android tests passed; arm64/x86_64 core builds passed. Windows build and four affected checks passed; see ../windows-stage-instrumentation. All eight trace footers have zero dropped events/rejected threads. Five-second diagnostic, not qualification; startup included.

Mean stages (1080p /4K milliseconds):

- Consumer AHB lock: .090/.074; normalization4.908/20.691; writeback1.049/3.378; unlock.041/.036.
- Consumer output allocation .127/.206; zero-fill plus row copies3.814/14.679. This scope includes memset and destination first-touch, NOT memcpy alone. Allocation timing excludes deferred first-touch cost.
- Producer AHB lock .140/.062; normalization6.074/20.977; writeback.922/4.691; unlock.038/.056; row copies.632/3.552.

The native CPU YUV normalization and consumer destination fill/copy dominate measured stage costs, not lock/unlock calls. The isolated cached-buffer copy is not equivalent to per-frame fresh GStreamer output allocation/first-touch. These are stage observations, not proof of exact per-byte memory causes. Existing mapping contract requires read/write preservation; no implicit read-only/write-discard assumptions are introduced. M6.8 will assess explicit negotiated CPU-efficient choices and bounded output reuse.

M6.5a remains open for cross-platform staging/waits and present boundaries; this benchmark does not measure physical presentation. Trace overhead comparisons belong to M6.6b.
