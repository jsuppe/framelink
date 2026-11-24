# Windows 4K30 source memory growth — 2026-11-24

**Question (Jon).** What makes the 4K30 source's private memory grow by about 40–44 KB/min? It has done so in every Windows 4K30 run so far:
- 42.0 and 43.9 KB/min on 2026-11-22;
- 40.7, 41.7, 40.7 and 43.5 KB/min at `357d82a5` ([record](../windows-native-relay-rate-357d82a/README.md)).

The relay and the receiver don't grow. The source doesn't grow at Windows 1080p60 (≤ 0.12 KB/min), or on Linux at 4K30 (0.0).

## Answer

**It is NVIDIA's D3D11 user-mode driver, not FrameLink code.** The bench's source uploads each frame from CPU memory with `ID3D11DeviceContext::UpdateSubresource`. At 4K, the D3D11 runtime takes its amortized path for large updates (`UpdateSubresource_Amortized`). In the traced run, the driver (`nvwgf2umx.dll`, 617.14) allocated **one 33 031-byte heap block about every 21.3 s under that call and never freed it**.

Each block extends the process heap's committed memory by one 36 KB step, which is the step size the resource sampler records. **The growth is bounded**: in all four 1800 s runs at `357d82a5` it stops at about 1300–1320 s. A 3600 s run confirmed it: memory stays flat from 1298 s to the end, for 2303 s ([follow-up](#follow-up-at-648fe9de-staging-upload-and-3600-s)). That is about 50 blocks, under 2 MB in total.

**It is not specific to `UpdateSubresource`.** When the source writes through a mapped staging texture and `CopyResource` instead, the same 36 KB steps appear, about 50 of them, every 18.9 s instead of 21.0 s, and they stop by 1171 s. A heap trace of that mode shows **the same 33 031-byte block from the same driver routine, reached through `CopyResource_Amortized`** ([follow-up](#follow-up-at-648fe9de-staging-upload-and-3600-s)). The driver keeps it in a code path shared by large 4K updates and copies.

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
- **Tied to the producer's CPU-to-GPU 4K upload, by either method.** The relay and receiver don't upload, and they don't grow. A producer that uploads 4K frames from CPU memory on this driver, with `UpdateSubresource` or through a mapped staging texture, should expect the same bounded growth. A producer that renders its frames on the GPU is not measured.
- **Bounded:** the growth stops by about 1320 s in every run, which is consistent with a fixed-size table in the driver. The 3600 s run shows it stays stopped for at least 2303 s.
- **Not established:**
  - **What the block is:** it is in NVIDIA's closed driver, which has no symbols.
  - **What sets the interval:** one block per 639 frames with `UpdateSubresource`, one per 566 with a staging upload and `CopyResource`. Both make one large D3D11 call per frame, so the trigger is not a plain count of those calls.
  - **Why 1080p doesn't trigger it:** not verified. (The earlier guess, that 1080p uploads don't take the amortized path, does not explain why a staging upload also grows.)

## Follow-up at `648fe9de`: staging upload and 3600 s

At the owner's request, two runs on the same host and driver tested this record's conclusions. Bench `648fe9de` adds two diagnostic options, with the defaults unchanged:
- `--upload staging` (Windows only): the source writes each frame into a CPU-writable staging texture (two, alternating), then `CopyResource`s it to the pool image;
- `--seconds` up to 3600. The checker still freezes qualification at 1800 s.

**Runs** (`followup/`; bench SHA-256 `f89e503d…`, plugin `ee7b6c70…`). The plugin hash differs from `357d82a5`'s `fcb9e0d0…` with no plugin source change, so the build is not byte-reproducible.
- **Build and smoke** (`m6l-build.ps1`, `build.log`): unit tests OK. A 20 s NV12 4K30 staging capture passes the analyzer (arrival max 9.30 ms) and records `"upload":"staging"`.
- **Run** (`m6l-formal.ps1`, `m6l-task.ps1`, `run.log`): in the desktop session, as for the lit re-run. The task woke the display at 11:42:18 and set the session hold correctly (`[Convert]::ToUInt32`). It then ran:
  1. the formal 4K30 cells with `--upload staging`, 1800 s each;
  2. a 3600 s NV12 4K30 capture with the default upload.
- **Display:** every 5-min `powercfg /requests` check from 11:47 to 13:42 listed the task and the bench under DISPLAY. The wake (566 reason 32) was the only display event. The script's application list came out empty, because window titles aren't visible from the ssh session; the owner's normal desktop was in use.

`steps.py` counts the 36 KB steps in each source's memory samples. On the lit `357d82a5` captures it reproduces the step table above (51 and 51).

| Run | Result | Arrival p50 / p99 / max (ms) | Upload call p99 / max (ms) | 36 KB steps | First / last step | Interval (median) | After the last step | Gate memory slope |
|---|---|---|---|---|---|---|---|---|
| staging, BGRA 4K30 1800 s | **pass**, zero misses | 10.03 / 11.54 / 20.87 | 2.01 / 2.58 | 51 | 76 / 1171 s | 18.9 s | flat for 630 s | 24.1 KB/min |
| staging, NV12 4K30 1800 s | **pass**, zero misses | 6.02 / 7.29 / 8.42 | 0.80 / 1.35 | 49 | 133 / 1171 s | 18.8 s | flat for 630 s | 24.2 KB/min |
| default upload, NV12 4K30 3600 s | zero misses (analyzer with the cap at 3600 s) | 5.98 / 7.18 / 12.84 | 0.69 / 4.16 | 52 | 65 / 1298 s | 20.9 s | flat for 2303 s | −0.02 KB/min |

- **Staging upload:** the growth is still there. The count of steps and the total are about the same; the steps come faster and end earlier. So the staging run disproves the earlier statement that a mapped-staging producer would not take this path.
- **3600 s:** the steps end at 1298 s, as at 1800 s, and memory is flat for the remaining 38 minutes. The gate's regression covers the second half of the run, which is entirely flat here, so its slope is −0.02 KB/min. At 1800 s, the same window includes the last ~400 s of growth, which is where the ~41 KB/min comes from.
- **Checker for 3600 s:** the 3600 s capture was analyzed with a scratch copy of `check_local_native_bench.py` with the duration limit raised from 1800 to 3600, the only change. Its analysis is `followup/nv12-4k30-3600s.analysis.json`. This is not a qualification cell.
- **Staging-upload cost:** the upload call is a CPU write into mapped memory plus a GPU copy. Its p99 is 2.01 ms for BGRA and 0.80 ms for NV12. Arrival timing is the same as with the default upload.

### Heap trace with the staging upload

`m6m-heap.ps1` is `m6k-heap.ps1` with `--upload staging` at `648fe9de`: NV12 4K30 600 s, heap and VirtualAlloc tracing with stacks from 101 to 377 s (`heap-run.log`). `outstanding.py` (new opt-in `--times`, which lists each allocation; the default output is unchanged) gives `outstanding-staging.txt`:
- **Heap:** 26 allocations still live at the end, 455.8 KB. Of those, **451.6 KB is 14 blocks of exactly 33 031 bytes**, all with one stack:

  `bench source upload` → `d3d11!CopyResource_Amortized` → `d3d11!CopyResource` → `nvwgf2umx.dll` (6 frames) → `ntdll!RtlAllocateHeap`

  The first five driver frames are the same addresses as in the `UpdateSubresource` trace (`…7ab153c`, `…7a92c71`, `…6cd01f1`, `…719261b`, `…7192761`). Only the frame below them differs (`…718a24b` here, `…7199f9b` and `…7189e77` there).
- **Timing:** one block every 18.87 s (566 frames at 30 fps), at run times 114.0 to 359.3 s.
- **VirtualAlloc:** 12 commits from the same stack, 452 KB. Each one matches a memory increase in the run's samples within 0.6 s (`heap-staging-source.resources.csv`), including a 52 KB commit at 170.6 s and a 40 KB one at 208.4 s, which match +52 and +40 KB steps. The blocks at 189.5 and 359.3 s fitted into memory already committed.
- **Everything else** is under 5 KB: thread-pool timers, GStreamer and thread start at 199 s.

## Files

- `m6k-heap.ps1`: the run and trace script. `run.log`: its output.
- `outstanding.py`: the analysis. `outstanding-top.txt`: its top groups.
- `config.json` and `source.resources.csv`: the traced run's config and source memory samples.
- Outside the repository: `dump.tgz` (107 MB), the symbolized trace dump, with its SHA-256 in `dump.sha256`.
- `followup/`: the `648fe9de` scripts and logs, `steps.py`, the staging heap trace (`m6m-heap.ps1`, `heap-run.log`, `outstanding-staging.txt`, `heap-staging-config.json`, `heap-staging-source.resources.csv`), each run's environment, config, summaries and analyses (`staging/`, `3600/`), and `steps.txt` (the step counts). Outside the repository: `followup-648fe9d.tgz` (69 MB, all captures with their CSVs and the task log) and `dump-staging.tgz` (the staging trace's symbolized dump with the run's config, memory samples and video CSV), with their SHA-256 in `followup/captures.sha256`.
