# Native reader completion handoff

Linux 14/14 affected tests; Windows/Caspar 14/14; Android tablet two new lease
checks passed and both ABI builds passed. Ledger ASan/UBSan and warnings-as-errors
passed. Raw logs retained here.

Lease completion accepts a duplicated POSIX completion FD or an imported D3D11
shared fence/value. The caller can close its input handle after successful handoff.
The lease token then expires: duplicate handoff, retain and synchronous completion
are invalid. Collection is nonblocking and retires only the completed prefix.
Native completion errors/device loss are not interpreted as permission to reuse.
This does not add image allocations or an artificial presentation queue.

Windows tests exercise real shared D3D11 fence objects and context signals.
Linux/Android use deterministic eventfd readiness controls, not GPU driver-created
sync files. Test completes the later reader first and proves producer buffers stay
unavailable until the earlier reader completes. Native GPU sync-file driver
qualification and actual downstream decoder/relay integration remain separate.

Lifecycle extension: Linux, Windows and Android targeted read_leases checks pass.
A disconnected producer cannot reconnect while a deferred reader fence is held;
after completion/collection, replacement attaches and old tokens remain invalid.
These use separate endpoint objects in one test process, not process-kill injection.
