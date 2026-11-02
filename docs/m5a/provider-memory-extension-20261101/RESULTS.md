# Task 7 partial: SessionProvider memory extension

Optional configureMemory callback runs before channel ownership transfer, while
GStreamer element is NULL. Mode bit 0x100 explicitly requires peer extension
support. Missing/one-sided support rejects, including producer-required/client
legacy. Unsupported mode bits continue to reject. Session v1 wire is unchanged.

Consumer creates/adopts its ring, then READY permits producer import. Both
exchange authenticated offers before producer starts either media pipeline.
Legacy copying elements cannot claim zero payload operations; Windows additionally
requires upload/readback declarations. Trusted provider remains responsible for
all additional operations, device/import capability and exact resource binding.

Linux six extension scenarios passed: permitted copy, strict rejection, binding
mismatch, hidden-copy rejection and both one-sided opt-ins. Failure cases deliver
zero video/PCM and no playback clock. Existing 17-case broker suite also passed.
Windows playback/producer/consumer builds and three control tests passed. Android
cross-app native arm64/x86_64 libraries built; no new signed-app runtime claimed.

The integration fixture deliberately uses a synthetic provider binding: control
flow and media gating evidence ONLY, not actual native allocation agreement or
zero-copy qualification. Real verified provider binding and decoder completion
integration remain required for task 7 closure.
