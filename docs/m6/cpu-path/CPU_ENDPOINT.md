# Authenticated shared-CPU endpoint transfer

POSIX fl_gst_session_create_cpu_endpoint accepts a dedicated connected Unix STREAM
and configured session/root credential. Both peers authenticate with the CPU
bootstrap domain; consumer creates a CLOEXEC SEQPACKET socketpair and transfers
one endpoint via SCM_RIGHTS. Producer requires exactly one descriptor, marker V,
no truncation, connected Unix SEQPACKET type, then acknowledges K. Both retain a
single owned endpoint on success and close the bootstrap stream. Caller retains
its GObject reference. Failure returns -1, closes received/generated FDs, closes
bootstrap and cancels session. Whole-call deadline and caller cancellation apply;
configuration is checked before acknowledgement and again before completion.

Tests: successful transfer and endpoint connectivity/CLOEXEC;
missing FD, duplicate FDs, STREAM/DGRAM substitution, wrong marker, silent
post-auth peer and20-FD ancillary truncation rejection. Every case returns to the
exact process FD baseline. Twelve existing thin/bin integrated CPU A/V cases now
use these authenticated transferred endpoints before pool binding, preserving
pixel/PCM/timing/terminal/conflict/death checks. Linux3/3 tests, Android2 runtime
binaries and both Android ABI builds pass. No failures occurred. Exact evidence
in cpu-endpoint/ (Linux includes av_auth built in the preceding prerequisite).

This is connected-socket provisioning, not filesystem discovery. The application
must protect discovery/root credentials and supply the bootstrap connection.
CPU session= still rejects; next integrate protected local Unix rendezvous and
bin setup without native fallback. Windows allocator, rate/endurance and full
M6 acceptance remain open; M7 maintenance hold unchanged.
