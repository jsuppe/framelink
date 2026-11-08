# Protected Unix rendezvous prerequisite

Linux/Android now have an internal abstract Unix STREAM listener with protected
current-user discovery. FAR2 stores an independent random 128-bit socket name
and 256-bit authentication credential. The socket name is public, never the
credential. Existing FAR1 TCP bytes are unchanged; typed readers reject the
other transport without modifying caller outputs. No TCP fallback.

The listener is CLOEXEC/nonblocking, bound/listening before publication, and
uses the existing owner-only directory, atomic record publication and persistent
OS lifetime lock. TCP and Unix publishers share exclusion. Close removes only
the matching generation record and closes the socket. Crash recovery uses fresh
name/credential; accepted connections still REQUIRE authentication. This is not
an authenticated session or bin setup by itself.

Validation: Linux4/4 (rendezvous/auth/transport/CPU A/V), Android3 runtime binaries
and both ABI builds, Windows3/3 compatibility tests. Tests cover real Unix
connect/accept/data, exclusion, fresh identity, crash recovery, malformed FAR2,
mixed endpoint rejection,100 publish/close cycles and existing access checks.
Initial Android runs exposed hard-link then FIFO fixture creation denied by
filesystem policy. Both unavailable negative cases are now explicitly reported,
not passed; Linux executes them. Original failures preserved. No production
security check relaxed. See unix-rendezvous/ for commands, hashes and logs.

Next: use this listener for authenticated local CPU session setup and bin wiring.
Windows shared-CPU allocation and rate/endurance gates remain open.
