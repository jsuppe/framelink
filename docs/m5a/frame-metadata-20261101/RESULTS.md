# Optional frame metadata transport

Parent-tested on Linux RTX3090, Windows Caspar, Android TCL API35.
Linux 4/4 (metadata contract/transport, reader leases, submit ownership).
Windows 2/2 metadata cases. Android device 2/2 metadata cases; arm64 and
x86_64 API29 builds pass (x86_64 build only).

Exact DTS, variable duration, discontinuity, stream generation and PTS survive
publication. Optional absence, slot reuse, required-mode refusal, invalid fields,
malformed greeting type/size, and unknown required features are checked.
POSIX case also delays readiness with a test eventfd; this is a readiness control,
not a new driver-sync-file qualification. Windows does not run that FD case.

Legacy public frame layout and wire version remain unchanged. This slice does
NOT wire GStreamer metadata or establish native sink/decoder/remote policy,
GPU throughput, audio timing, or physical presentation qualification.
