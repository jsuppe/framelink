# M6.8 Linux CPU-readback investigation

November 7, 2026; source revision `24bcd78`; RTX 3090, NVIDIA 580.178.04.
This completes the reproduction/allocation diagnostic **8a**, not production
repair 8b or target-rate qualification 8c. All probes are sequential and local.
Raw results, source, process resources, negative attempts and provenance are
retained here. Executables remain in `<linux-home>/dev/evidence/m6-cpu-path`.

## Discriminating results

1. Original and recovered FrameLink allocation probes each contain 96 checked
   rows. Recovery exit 0. Cached heap-copy controls are materially faster than
   DMA-BUF reads. Streaming loads improve reads but do not solve target rates.
   Map-call time is not the dominant cost. This does not establish a particular
   physical cache attribute or attribution of every slow phase.
2. `gbm-staging-20261107`: 64 exact-pixel checked rows, exit 0 in 116.51 s.
   RENDERING|LINEAR allocation rejected at both sizes; LINEAR and LINEAR|WRITE
   accepted. Alternating direct-mmap versus `gbm_bo_map` reads found no efficient
   staging path. Adding WRITE is also not a repair. Both slow phases and maxima
   remain in the results; do not compare only favorable iterations.
3. `gl-staging-20261107`: sample the imported allocation into a private RGBA
   framebuffer, finish the GPU draw, then read the whole framebuffer to cached
   CPU storage. This is explicitly a **GPU copy + readback**, not zero-copy.
   All 16 uniform-frame checks pass only after per-frame EGL image rebinding.
4. `gl-staging-pattern-20261107`: 32 frames at each target size, 64/64 full-frame
   checks pass, exit 0. The independent expected pattern varies x, y, frame and
   channel; BGRA source is checked against the corresponding RGBA output.
   Dithering is disabled. Imported image and texture are retained between frames,
   but `glEGLImageTargetTexture2DOES` is called again before every draw.

| Pattern probe | Draw + finish median | Full readback median | Readback maximum |
|---|---:|---:|---:|
| BGRA 1920x1080 | 0.049 ms | 1.035 ms | 1.131 ms |
| BGRA 3840x2160 | 0.125 ms | 3.776 ms | 6.581 ms |

These are **stage costs**, not end-to-end added-hop latency or paced throughput.
CPU generation, allocation, validation and release are outside the reported
readback interval. First-import cost is included in draw for the first frame.
No A/V, NV12, device-loss, cross-process or concurrent renderer qualification is
implied. The GL output is RGBA; preserving BGRA in a production path needs an
explicit channel arrangement/format check. No production code is changed.

## Visibility negative (must not be discarded)

Retaining an imported texture and merely rebinding its texture name does not
refresh all externally CPU-written pixels on this driver. The uniform probe
first failed at frame 1; disabling dithering did not repair it. The pattern
probe's otherwise-identical **no image rebind** control fails at frame 1,
x=880,y=16,R: expected150, actual143 (the preceding frame's value).
Rebinding the EGL image before each draw passes all 64 pattern frames.
This supports a refresh/visibility mechanism, not a universal driver diagnosis.
It also reveals a coverage gap in the existing benchmark helper, which samples
only its identity header and final pixel; neither proves all pixels are current.
Do not silently reinterpret historical sampled-pixel passes as full-frame proof.

## Implementation direction and remaining gates

Stop repeating identical allocation/map controls. Explicit GL staging is a
promising Linux GPU-allocation→CPU path. Cached shared-CPU allocation remains a
separate candidate for CPU-only producers/consumers; the heap-copy control is
not an implementation or cross-process validation of that candidate.

Next bounded task: turn the image-refresh negative into a durable full-frame
regression, repair the reusable benchmark helper's external-write refresh, and
validate Linux/Android affected native consumers. Then implement an **explicitly
selected and negotiated** Linux staging path without changing native defaults.
Do not smuggle readback into a CPU-copy-only or strict native agreement:
`FL_MEMORY_COPY|FL_MEMORY_READBACK` must be permitted and reported, including
payload byte counts. Preserve source leases until GPU completion; make context,
thread, generation, image-cache invalidation and error cleanup ownership explicit.
Reject unsupported layouts rather than treating shader NV12 conversion as exact
plane preservation. Compare a real shared-CPU allocation path before closing 8b.
Target-rate A/V and all-platform regressions remain 8c/final-integration work.
