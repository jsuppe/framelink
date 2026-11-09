# Windows native producer layout reuse

Base `b4eb366b4837d8132da7b6643d5aa300fe4d18ea`; exact changed source SHA256 values
are in status.json. Staged Windows build passed, followed by **7/7** focused
checks (five native/session/policy tests and two A/V-bin tests). No Linux/Android
campaign: production change is confined to the Windows D3D11 producer header.

## Mechanism and bounded repair

The producer previously called `gst_d3d11_allocator_alloc_wrapped` with size zero
on every reservation. That requests GStreamer's temporary staging-layout probe
on every frame, despite the ring allocation being unchanged. The native receiver
already avoids the repeated probe. The producer now caches only measured size
and pitch per slot, keyed by ring generation and texture identity. It retains no
GstMemory, COM reference or lease in that cache. A new generation reprobes even if
a texture address is recycled. Query current geometry before describing the
buffer, so growth/shrink cannot leave GstVideoMeta using the constructor's size.

No CPU/GPU completion fence, producer reference, cancellation, strict foreign-
allocation rejection, public API, wire format or fallback/default changes.
Explicit CPU maps still request fresh download; this does not remove the initial
layout probe or qualify end-to-end zero-copy.

## Verification

New test exercises RGBA and NV12, each with 32 distinct pixel values at each
64→640→64 width step (192 buffers total). It checks exact dimensions, plane
count, strides, extent and every active pixel through explicit oracle readback.
Each reservation is cancelled only after readback completes. Existing tests
retain buffers through remote completion and teardown, reject foreign buffers,
check original allocations, ready fences and authenticated A/V with37ms offset,
both terminal orders and empty streams.

Native producer test passed in18.72s; source/policy/session tests passed; both
A/V-bin tests passed. All logs are retained here. Clean exact-revision full suite
is the next integration gate.

## Acceptance limits / next action

This removes an evidenced repeated driver-work mechanism; no new timing or
private-commit slope is claimed. It does **not** explain or waive the original
startup1529ms or21.10/84.09KiB-per-cycle lifecycle failures. Those remain owned by
Evelyn, suspended pending a matched mechanism-specific control. Real decoder and
two-hop integration remain open; M6 open, M7 hold. Loom pre-edit context could
not load because its chromadb dependency is unavailable; no worker delegated.
