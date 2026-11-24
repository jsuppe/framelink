# Windows 4K30 source memory growth — 2026-11-24

**Question (Jon).** What makes the 4K30 source's private memory grow by about 40–44 KB/min? It has done so in every Windows 4K30 run so far:
- 42.0 and 43.9 KB/min on 2026-11-22;
- 40.7, 41.7, 40.7 and 43.5 KB/min at `357d82a5` ([record](../windows-native-relay-rate-357d82a/README.md)).

The relay and the receiver don't grow. The source doesn't grow at Windows 1080p60 (≤ 0.12 KB/min), or on Linux at 4K30 (0.0).

## Answer

**It is NVIDIA's D3D11 user-mode driver, not FrameLink code.** The bench's source uploads each frame from CPU memory with `ID3D11DeviceContext::UpdateSubresource`. At 4K, the D3D11 runtime takes its amortized path for large updates (`UpdateSubresource_Amortized`). Inside it, the driver (`nvwgf2umx.dll`, 617.14) allocates **one 33 031-byte heap block about every 21.3 s and never frees it**.

Each block extends the process heap's committed memory by one 36 KB step, which is the step size the resource sampler records. **The growth is bounded**: in all four 1800 s runs at `357d82a5` it stops at about 1300–1320 s and stays flat for the last 480 s. That is roughly 50–60 blocks, about 2 MB in total.

## Shape of the growth (the 1800 s cells at `357d82a5`)

| Run | 36 KB steps | First / last step | Interval (median) | After the last step |
|---|---|---|---|---|
| lit, BGRA 4K30 | 51 | 129 / 1298 s | 21.0 s | flat for 500 s |
| lit, NV12 4K30 | 51 | 44 / 1319 s | 21.0 s | flat for 480 s |
| first, BGRA 4K30 | 49 | 150 / 1298 s | 21.0 s | flat for 500 s |
| first, NV12 4K30 | 47 | 171 / 1319 s | 20.9 s | flat for 480 s |

- **Same in both formats:** the step size and interval are the same for BGRA (33 MB frames) and NV12 (12 MB frames), so the growth doesn't scale with the frame size.
- **Why the slope is ~41 KB/min:** the gate's regression over the window averages the growth phase and the flat tail.

## The trace

`m6k-heap.ps1`, a diagnostic run, not a qualification run:
- **Run:** NV12 4K30 for 600 s at `357d82a5`, on the normal desktop.
- **Heap tracing:** from 101 s to 378 s, xperf traced the source process's heap, with an allocation stack for every allocation.
- **VirtualAlloc tracing:** kernel VirtualAlloc/VirtualFree with stacks.
- **Symbols:** the dump was symbolized with Microsoft's public symbols. There are no symbols for the NVIDIA modules or the bench, which shows as offsets.

`outstanding.py` pairs every allocation with its free, and groups whatever is still live at the end by allocation stack (`outstanding-top.txt`):
- **Heap:** 420 122 allocations; 73 still live at the end, 405 KB. Of those, **387 KB is 12 blocks of exactly 33 031 bytes**, all with the same stack:

  `bench source upload` → `d3d11!UpdateSubresource_Amortized` → `d3d11!UpdateSubresource` → `nvwgf2umx.dll` (7 frames) → `ntdll!RtlAllocateHeap`

  The rest is under 3 KB in total, from one thread start at 200 s (TLS, CRT and driver per-thread data).
- **VirtualAlloc:** 324 KB of the 364 KB of live commits is 9 commits of 36 KB, with the same driver stack. Each is the heap committing more memory for one of those blocks.
- **Timing:** the blocks were allocated at run times 107.1, 128.4, 149.7, 170.9, 192.2, 213.5, 234.7, 256.0, 277.3, 298.5, 319.8 and 341.1 s. That is one every 21.3 s, which is 639 frames at 30 fps. The resource sampler (`source.resources.csv`) records a +36 KB step within a second of each of the nine blocks that needed a new commit: 149.5, 171.4, 192.3, 213.2, 235.2, 256.1, 299.0, 319.9 and 340.8 s. The other three fitted into memory already committed.

## What this means

- **Not a FrameLink leak.** No outstanding allocation comes from `framelink.dll`, the GStreamer plugin or the bench's own code, other than the call into D3D11.
- **Limited to the harness's upload path.** The relay and receiver never call `UpdateSubresource`, and they don't grow. A producer that writes frames on the GPU, or through a mapped staging texture, would not take this path. A producer that uploads 4K frames with `UpdateSubresource` on this driver would see the same bounded growth.
- **Bounded, so far:** the growth stops by about 1320 s in every run, which is consistent with a fixed-size table in the driver. The stop is observed over 480–500 s; growth beyond 1800 s is not measured.
- **Not established:**
  - **What the block is:** it is in NVIDIA's closed driver, which has no symbols.
  - **Why 1080p doesn't trigger it:** presumably 1080p uploads (8 MB) don't take the amortized path, but that is not verified.

## Files

- `m6k-heap.ps1`: the run and trace script. `run.log`: its output.
- `outstanding.py`: the analysis. `outstanding-top.txt`: its top groups.
- `config.json` and `source.resources.csv`: the traced run's config and source memory samples.
- Outside the repository: `dump.tgz` (107 MB), the symbolized trace dump, with its SHA-256 in `dump.sha256`.
