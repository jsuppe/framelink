# M5a.12 — platform GPU relay qualification (in progress)

Windows/Caspar RTX5090: `native_relay_process_win` passed16/16 NV12 frames
across three actual processes. Two canonical NT allocations, reversed downstream
pool order, exact pixels/PTS/DTS/duration. Explicit inherited-handle whitelist;
no payload copy/map in relay. The original producer and final verification readback
are deliberately CPU-readable; this is not a GPU-decoder benchmark.

For every frame the final reader holds a lease and handshakes with the producer:
with the other slot reserved, producer nonblocking acquire must time out. Then the
consumer issues a GPU CopyResource into a test-only verification staging image,
signals a D3D11 fence, attaches that fence to its read lease and collects completion.
Only downstream completion allows relay to retire upstream; producer checks the
returned allocation matches the submitted original. All output bytes are verified.
Final producer-return acknowledgement precedes relay shutdown, preventing a fixture
race between final slot retirement and closing its upstream channel.

This is bounded correctness/ownership qualification, not sustained FPS, GPU decoder
sharing or physical scanout. Android three-process GPU and remaining integration
coverage still pending; task12 not complete. Throughput/multi-hop latency task13.

## Linux and Android three-process GPU completion

Shared `fl_native_relay_process_egl` fixture also passed16/16 exact RGBA frames on
Linux RTX3090/NVIDIA EGL and the Android API35 Mali-G52 tablet. Original allocation
handles travel by SCM_RIGHTS on Linux and AHB Unix-socket transfer on Android.
Relay matches exact IDs/layout (not slot index), performs no payload map/copy,
and holds upstream leases until downstream consumed fences retire. The final
consumer imports each original allocation into EGL, samples it into a verification
FBO and attaches an exported native sync FD to its read lease. All2048 pixels/frame
and original PTS/DTS/duration are checked. Each frame's producer cannot acquire a
slot while the final reader holds its lease; both slots are accounted for.

arm64/x86_64 builds passed; arm64 device run passed, x86 runtime not claimed.
An initial missing AHB declaration include was corrected before runtime. Linux and
Android use actual separate exec'd processes with bounded IPC waits and a terminal
producer acknowledgement before channel teardown. CPU pattern generation and
verification readback are deliberate, not hidden relay operations. Existing local
peer-death fixtures remain separate evidence that disconnect does not imply safe
reuse. These are same-UID harnesses, not signed cross-UID native A/V qualification.

Task12 remains open for signed native pipeline integration and final platform
matrix reconciliation. Task13 rate/residence/trace-overhead gates remain unrun.
