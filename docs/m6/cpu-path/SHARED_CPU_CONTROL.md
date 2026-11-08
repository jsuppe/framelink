# Cross-process cached shared-CPU control

Source and all 32 raw rows: `shared-cpu-20261107/`. Exit 0. Four cells, eight
iterations each, BGRA and NV12 at 1080p/4K. This is a real size-sealed memfd, not
just a cached heap baseline: forked consumer removes the inherited producer
mapping and creates its own read-only shared mapping. An explicit socket ACK
follows every complete pixel check before the producer may overwrite storage.

The independent byte pattern varies frame, plane, row and column. Plane offsets
are page-aligned and row strides 256-byte aligned. Every logical byte is checked;
padding is not claimed to be image data. Both endpoints perform an explicit row
copy. The size seals reject allocation resizing; they do not make pixel payload
immutable. Descriptor inheritance is not a FrameLink descriptor-exchange test.

| Cell | Producer copy median / max | Consumer copy median / max |
|---|---:|---:|
| BGRA 1080p | 0.611 / 2.675 ms | 0.567 / 0.936 ms |
| BGRA 4K | 3.357 / 12.220 ms | 3.430 / 4.543 ms |
| NV12 1080p | 0.109 / 0.908 ms | 0.111 / 0.204 ms |
| NV12 4K | 1.222 / 4.083 ms | 1.058 / 1.285 ms |

Cold first-touch costs are retained. Timing excludes independent pattern
generation, pixel verification and IPC handshake. This is not paced throughput,
end-to-end latency, A/V, GPU interoperability, or lifecycle qualification.

## Bounded implementation choice

The two demonstrated choices serve different use cases:

- Existing native GPU allocations: explicit GPU copy + CPU readback (implemented
  for Linux BGRA/RGBA in 6275fe5), subject to negotiated permissions.
- CPU-only producer/consumer: explicitly negotiate `FL_MEMORY_SHARED_CPU`, map
  cached shared storage and report actual endpoint copies. This avoids routing
  CPU content through a GPU allocation just to map it back. NV12 planes can be
  preserved without shader color conversion.

Do not label a memfd as DMA-BUF, send it under an existing native descriptor,
claim GPU/device capabilities it lacks, or change existing native defaults.
A coordinated shared-CPU path is **not implemented** by this control. Next work
must define a separately negotiated resource/descriptor contract, bounded layout
and size validation, mapped lifetime and generation ownership, and explicit
rejection of native GPU export/import calls on CPU-only images. Add malformed
layout, missing-copy permission, retained-frame, reconnect and resize negatives
before connecting it to the coordinator/GStreamer bins. M6.8b/c stay open.
