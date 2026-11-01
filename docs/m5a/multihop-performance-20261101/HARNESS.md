# Native transport comparison harness

`fl_native_boundary_benchmark` is a two-allocation CPU-readable RGBA workload.
It forks/execs a paced producer and, for two boundaries, a read-only same-allocation
relay. Only startup and terminal-drain control handshakes exist; none per frame.
Producer cadence is absolute steady-clock60Hz. Every allocation's original image
identity, PTS/DTS/duration and generation remain unchanged. The source fills all
logical image bytes; consumer checks embedded identity/timing and sampled pixels.
Native allocation identity matching is inherited from the GPU relay fixture.

The timestamp embedded by the source is a **CPU-write marker before unmap and
submit**, not a GPU-ready timestamp. Marker-to-arrival includes unmap/cache work,
submission and transport. Trace joins separately measure submit-entry to acquire-
return. Consumer map/verification/release cost is reported separately. No claims
of GPU completion, presentation, PCM delivery or driver-internal copies follow.

Default3720 frames,120 warmup,1920x1080,60fps. Set `FL_BENCH_HOPS=1` then2 and a
unique `FL_BENCH_OUT` prefix per case. CSV writes happen after terminal drain.
`FL_BENCH_DELAY_US` and `FL_BENCH_DELAY_FRAME` inject final-reader delay. A missing
or repeated identity aborts the run; it cannot be hidden by an average FPS number.
Use `tools/check_boundary_benchmark.py ONE_PREFIX TWO_PREFIX` for the declared
nominal rate and extra-boundary p95 gates. A delayed-reader workload is not a
nominal no-drop run; record its outcome separately. A source that falls behind
must still meet measured delivery rate to pass.

Trace with `FRAMELINK_TRACE=PREFIX` and bounded `FRAMELINK_TRACE_EVENTS=1048576`.
Confirm every file's zero-loss footer and exact channel/fence/PTS joins. Enabled
recorder reservation/memory and exit flush are separate overhead, not hidden from
qualification. Multi-hop PCM and GPU-output timing remain additional task13 gates.

Optional `FL_BENCH_GPU=1` selects an EGL GPU consumer instead of CPU mapping. It
samples each original shared allocation into a private full-resolution FBO,
attaches a native completion fence to the reader lease, and checks a32-byte header
plus a color pixel from that private output. Imports/shaders/output resources are
cached, not recreated every frame. This deliberate verification readback is
outside the shared source path. Consumer retirement timestamp is recorded after
fence retirement, before private-output readback. No shared input CPU map is
permitted in GPU mode; source CPU pattern generation remains explicit.

Build/runtime qualification of this optional path is recorded separately from
CPU-only results. Neither mode carries PCM; existing A/V timing tests must not be
misrepresented as simultaneous PCM qualification of this benchmark.
