# Exact Windows IPC framing

Windows message-mode control transport previously accepted an FLK1 header whose
payloadSize was smaller than the received message, silently discarding trailing
bytes. New regression reproduced this failure before the one-line repair.
Receiver now requires payloadSize == bytesRead - headerSize; short header, bad
magic, missing payload, trailing bytes and UINT32_MAX length all fail closed.
Valid empty and nonempty messages remain accepted. Wire version is unchanged;
malformed traffic is rejected, not a compatibility mode.

Focused test passes all7 cases. Rebuilt Windows framelink and import-failure test;
21 affected IPC/D3D11/queue/lifetime/native-GStreamer/session/fault tests pass.
Logs and changed-source SHA256 are adjacent. Remote is a staged source tree,
not an exact-head git checkout; final integrated M6 qualification remains open.
Test originally referenced a newer CPU message absent in the staged protocol
header; corrected to legacy Hello because this test exercises framing only.
This repair does not add Windows CPU transport nor make blocking pipe I/O bounded.
