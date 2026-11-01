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
