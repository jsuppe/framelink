# Windows native-layout allocation attribution

## Verdict: PARTIAL

Question: does the native consumer's per-frame GStreamer D3D11 layout probe contribute avoidable private-commit growth?

GStreamer1.28.0 source: `gst_d3d11_allocator_alloc_wrapped(...,size=0)` creates/maps/frees a temporary staging texture to discover CPU layout. `get_resource_stride` exposes that probe's RowPitch. Upstream source consulted: https://github.com/GStreamer/gstreamer/blob/1.28.0/subprojects/gst-plugins-bad/gst-libs/gst/d3d11/gstd3d11memory.cpp . Installed runtime reported1.28.0.

Standalone A/B/A/B holds one3840x2160NV12 shared texture; compare size0 on every wrapper with reuse of its measured12441600byte/pitch3840 layout. Every wrapper aliases the same resource. Cached wrapper CPU map also verifies measured pitch and accessible size. No payload is produced; this is not transport qualification.

The initial accelerated no-Flush control reached roughly73GB private commit after6000temporary probes and released it at device/process teardown, while cached control stayed flat. This is a deliberately incomplete GPU submission model, NOT representative production leakage: production already Flushes consumed-fence retirement. Do not use it as the causal acceptance result or repeat that stress unnecessarily.

With Flush after each wrapper destruction, probe mode first allocates one12MB staging footprint then continues growing (both6000-frame trials), whereas cached mode stays flat after warm-up. A60000frame flushed control grows1588KB between frames10000and60000 (32.52bytes/frame); cached layout grows0KB in that interval. At30fps this mechanism is on the order of57KB/min, consistent with a contributor to native-consumer growth, not proof that every observed byte or rare timing stall is explained. All raw samples retained.

Repair: fixed-size per-slot layout metadata in NativeSourceOwner, keyed by ring generation and exact texture pointer. Probe actual layout once per allocation; later fresh GstMemory wrappers receive the measured size and GstVideoMeta pitch. Cache holds no texture/GstMemory references or leases. Existing frame leases, release Flush, readiness and explicit CPU-download flag are unchanged. Generation changes and source recreation force reprobe. Public D3D11 wrapped-memory API permits explicit size; the wrapper's resource-stride query may be unset until its first CPU map, so video layout remains carried by standard GstVideoMeta.

Windows affected checks cover cached-slot fresh-pixel readback, alias/copy negative, retained-memory lifetime, padded NV12/RGB10 and normal transport smoke. Native generation resize coverage is included. Sustained resource and final integration gates remain required; original159ms stall and BGRA CPU growth are not claimed repaired.

Validation: Windows initial affected5/5 passed (native alias/readback/lifetime and deliberate-copy negative, NV12/RGB10 roundtrip, transport smoke); live resize and policy-generation2/2 passed after fixing the fixture. The initial appsink resize fixture correctly failed because its reusable GstSample retained a native reader: replace with fakesink handoff and query accepted geometry, never relax ownership. Failed fixture logs retained. Linux rebuilt plugin and benchmark transport smoke1/1 passed. Full Windows rebuild/suite and sustained resource validation pending.

Full rebuild initially stopped before CTest: a final atomic-dimension test edit introduced a comma-separated declaration inside G_CALLBACK(lambda), which the preprocessor split into macro arguments. Separated declarations; native-source (including resize) and deliberate-copy negative2/2 now pass. Production cache code unchanged. Failed build and corrective validation retained; full suite restarted at the corrected test revision.
