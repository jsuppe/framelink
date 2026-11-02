# Producer-local reference ownership

Linux 3/3, Windows Caspar 3/3, Android tablet 3/3 affected native executables
passed. Android arm64/x86_64 API29 builds passed; x86_64 build only.

New buffer_references case fills all 256 reference tokens, checks bounds and
unchanged error output, publishes/releases remotely while local refs remain,
checks every remaining hold blocks reuse, completes the final reference on a
separate thread, and verifies prompt reuse even while another remote frame is
unfinished. Cancellation does not erase pins; geometry changes remain busy until
the last pin is released. Stale/double completion rejects. Existing reader leases
and submit ownership regressions pass. No pixel copy, allocation queue or fixed
presentation delay added. Real GPU work completion remains caller responsibility.

This is the ownership prerequisite for native producer GstMemory, NOT a completed
GStreamer allocation pool, decoder handoff, remote policy or throughput result.
