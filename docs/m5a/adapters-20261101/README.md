# M5a.2 actual-resource adapters — 2026-11-01

Read-only `flImageMemoryInfo` on actual allocated/imported ring resources, not
GPU-decoder integration or proof of physical VRAM placement.

Final source tests: Linux 6/6, Windows/Caspar 6/6, Android35 tablet
A06B4AABFB07983 6/6. Each includes policy/C11 regressions plus BGRA8, NV12,
RGB10A2 and RGBA8 resources. No skips. Android NDK28.2 API29 arm64 and
x86_64 builds passed; x86 runtime not tested. Raw logs retained as gzip.

Each hardware case allocates a two-image ring, opens its producer, checks
actual format/layout/device/mapping/synchronization facts, rejects foreign
channel and malformed query requests without modifying output, maps/fills,
submits/acquires, checks pixels/PTS, and verifies map lifetime transitions.
Strict policy rejects known staging/normalization operations; explicit allowed
operations accept them. No external-pool adoption or downstream relay is
advertised. Current ring import/native borrowed handles are advertised.

Reproduce desktop after building fl_memory_resource/fl_memory_contract/fl_memory_c_api:
`ctest --test-dir BUILD -C Release --output-on-failure --timeout 20 -R '^memory_(contract|contract_c_api|resource_[1-4])$'`
Android runs the two contract executables then `fl_memory_resource N`, N=1..4.
Hardware-unavailable returns77 (CTest skip), never silently counted as a pass.

Limits: native GPU layout is opaque on Windows/AHB. Linux imported descriptors
do not carry allocator usage/device identity; these are unknown rather than
inferred. Android unmapped YUV CPU normalization cost is unknown. Sync flags
identify current FrameLink API/transport, not qualification of a driver's Vulkan
semaphore import/export. Separate runtime fence/import gates remain M5a.12.
Physical scanout, throughput and audio timing were not measured in these tests.

Review finding for later strict allocation negotiation: Linux's existing
RENDERING|LINEAR fallback may allocate LINEAR-only. Query reports actual flags;
this task does not change legacy allocation policy. Strict required-render paths
must reject missing usage rather than treat requested flags as capability.
