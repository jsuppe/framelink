# Linux lifecycle memory steps — 2026-11-24

**Question.** Three Linux 4K30 lifecycle cells at `e1284833` fail the 16 KB/cycle memory gate on resident-memory steps the size of the frame storage ([record](../primary-e128483/README.md#the-linux-memory-failures-are-storage-sized-steps)). What allocates and releases those blocks?

## Answer

**Frame-sized system-memory buffers that are allocated and freed every cycle, and that glibc's malloc sometimes keeps resident afterwards.** Nothing is leaked: every frame-sized allocation is freed in the same cycle. The steps are freed frames still resident in glibc's per-thread malloc arenas at the moment the bench samples the long-lived peer, after each cycle's teardown.

- **Which buffers:** two kinds, depending on which side is long-lived:
  - **Long-lived producer:** each cycle builds a new pipeline, whose `videotestsrc` (the harness's test source) allocates its frames from GStreamer's default video buffer pool. That is 3 frames per cycle for BGRA 4K (33 177 743 bytes each) and 2 for NV12 4K (12 441 743 bytes), allocated on the cycle's `vsrc:src` streaming thread (`stack-producer-videotestsrc.txt`).
  - **Long-lived consumer, shared-CPU path:** FrameLink's `framelinkavsrc` prepares its CPU output pool at start: `srcCpuOutputBuffer` (`gst/gstframelink.cpp:1151`) from `srcSharedCpuStart`, 2 frames from GStreamer's default system-memory allocator, on a `pool-2` thread (`stack-consumer-cpu-output.txt`).
- **Why they stay resident:** each 4K frame is under glibc's 32 MiB cap for the dynamic mmap threshold. Once that threshold has risen, such blocks are carved out of a per-thread arena instead of getting their own mapping. A freed block returns to the OS only if the arena trims it. After a typical cycle, freed frames worth 35–63 MB are still resident (A's median against B's). The steps are cycles where one or two more frames are, up to 126.6 MB in total.
- **Not DMA-BUF:** the native path's DMA-BUF mappings (`rw-s /dmabuf:`) show 0 KB resident in every sample. The step memory is private anonymous memory in 64 MiB-aligned arena heaps.
- **Underneath:** without the steps, every cell grows by 1.3–2.3 KB/cycle, as recorded.

## Method

Diagnostic runs, not qualification runs. No repeat replaces a recorded failure. The runs used the same Linux host, revision and binaries as the recorded failures: `fl_gst_av_bench` `63695530…` and `libgstframelink.so` `f181428c…`, built at `master` `17afbe81`, byte-identical to `e1284833`'s.
- **Cells:** each failing cell exactly as `m6_bench_matrix.py` runs it: 100 cycles of 5 s, 4K30, primary profile, `G_DEBUG=fatal-warnings`.
- **`smaps_sampler.py`:** once a second, the long-lived peer's `/proc/PID/smaps_rollup`, and every mapping of 8 MiB or more in `/proc/PID/smaps` with its permissions, name and resident split.
- **`bigalloc_trace.c`:** an `LD_PRELOAD` shim that logs every allocation of 8 MiB or more in every bench process, with thread name and backtrace, plus the matching frees.
- **Two runs per cell (`run_cells.sh`):**
  - **A:** as recorded.
  - **B:** with glibc's mmap threshold fixed at 8 MiB (`GLIBC_TUNABLES=glibc.malloc.mmap_threshold=8388608`). Every frame-sized block then gets its own mapping and is unmapped when freed. Nothing else changes.
- **Analysis:** `analyze.py` applies the record's step definition: cycles more than 2 MB from the median of cycles 5–99.

## Results

| Cell (4K30, 100 cycles) | Run | Gate slope (KB/cycle) | Steps | Slope without steps | Median after teardown | Frame-sized allocations / frees in the long-lived peer |
|---|---|---|---|---|---|---|
| native BGRA, restart consumer | A | **139.27** (fail) | 14 × +63 MB | 1.35 | 81.2 MB | 300 / 300 (`vsrc:src`) |
| | B | 1.97 | 0 | 1.97 | 17.9 MB | 300 / 300 |
| shared NV12, restart producer | A | **17.19** (fail) | 2 × −12 MB (cycles 5–6) | 1.31 | 51.3 MB | 200 / 200 (`pool-2`, FrameLink's CPU output pool) |
| | B | 1.61 | 0 | 1.61 | 15.8 MB | 200 / 200 |
| shared NV12, restart consumer | A | **19.62** (fail) | 11 × +24 MB | 1.60 | 77.2 MB | 200 / 200 (`vsrc:src`) |
| | B | 2.34 | 0 | 2.34 | 18.2 MB | 200 / 200 |

- **A:** all three cells fail again, with the same signature as recorded: steps of exactly one or two frames. The slopes differ from the recorded 176.66, 25.32 and 75.11 because they depend on where the steps fall.
- **B:** all three pass, with no steps. The slopes match A's slopes without the steps. The long-lived peer's memory after teardown varies by only 0.2–0.3 MB across cycles 5–99. The median drops by the retained frames: 63.3 MB for BGRA, 35.5 and 59.1 MB for NV12.
- **Same work in A and B:** identical allocation counts, and every cycle completed (100/100). Only where freed memory goes differs.
- **Fresh peers:** each fresh process's frame-sized allocations are freed too (`per-process.txt`). Native fresh consumers make none.
- **Sampler caveat:** the sampler reads the rollup and the per-mapping list one after the other, so two of the 473 samples in native A caught a frame block between the two reads. Per-cycle values come from the bench's own summary, not from the sampler.

## What this means

- **Not a FrameLink leak.** The memory is allocated and freed every cycle. What the gate sees is glibc keeping some of it resident.
- **The gate measures it as defined.** It counts resident memory after teardown, so the cells fail as recorded. The steps are real resident memory at that moment, and an application that allocates frames from GStreamer system memory on glibc would see the same retention.
- **Not established:**
  - why the arena trims in some cycles and not others;
  - whether the retention is bounded over longer runs. Here it was at most 126.6 MB (BGRA) and 82.8 MB (NV12) above B's level at any cycle end.

## Options

1. **Harness:** run the Linux bench processes with a fixed mmap threshold (B's setting), through the runner or a `mallopt` in the bench. The gate is unchanged, and the cells' memory becomes deterministic. It also changes how FrameLink's own CPU output pool behaves under test, so the consumer-side steps would be hidden rather than removed.
2. **Product, consumer side:** give `framelinkavsrc`'s CPU output pool an allocator whose memory goes back to the OS when freed, for example one mapping per buffer. That removes the consumer-side steps for applications too. On its own it does not change the producer-side steps, which come from the harness's `videotestsrc`.
3. **Gate:** sample the long-lived peer after `malloc_trim(0)`, or exclude freed arena memory. That would be a gate change.

Options 1 and 2 can be combined: fix the product pool (2), and apply the harness setting (1) only to the test source's side, or document it.

**Decision (Jon, 2026-11-24): 2, plus 1 for the producer-side processes only.** FrameLink's own pool is fixed. The harness setting covers only processes whose frame-sized allocations are the test source's, so FrameLink's consumer-side code stays measured with glibc's defaults, and the gate is unchanged.

## Fix at `91053ff9`

- **Product (`gst/gstframelink.cpp`):** on glibc, `framelinkavsrc`'s CPU output pool uses an allocator that gives each of its two cached buffers its own anonymous mapping, unmapped when the memory is freed. The memory is ordinary wrapped system memory. Windows and Android are unchanged. The uncached fallback, used only when downstream holds both cached buffers, keeps the default allocator. It made no allocations in these cells.
- **Harness (`test/fl_gst_av_bench.cpp`):** on glibc, the roles that run the harness's `videotestsrc` (`producer`, `lifecycle-producer`, `source`) fix `M_MMAP_THRESHOLD` at 8 MiB with `mallopt`. Consumer roles keep glibc's default. Every summary records the setting as `harness.malloc`.
- **Tests:** 187/187 on the Linux host (`read_lease_egl_delayed_completion` skipped, as before).

**Rerun (C), diagnostic, not qualification:** the three failing cells, 100 cycles each, exactly as A, with no `GLIBC_TUNABLES` (`run_fixed.sh`). Binaries: `fl_gst_av_bench` `bcfe4e26…`, `libgstframelink.so` `8122c637…`.

| Cell (4K30, 100 cycles) | Gate slope (KB/cycle) | Steps | Median after teardown | A / B median | Frame-sized `malloc` calls in the long-lived peer |
|---|---|---|---|---|---|
| native BGRA, restart consumer | 2.42 (pass) | 0 | 18.0 MB | 81.2 / 17.9 MB | 300, all freed (`vsrc:src`, own mappings) |
| shared NV12, restart producer | 1.78 (pass) | 0 | 15.7 MB | 51.3 / 15.8 MB | **none**: the pool maps its buffers directly (the 200 counted are the 100 fresh producers', 2 each) |
| shared NV12, restart consumer | 2.29 (pass) | 0 | 18.2 MB | 77.2 / 18.2 MB | 200, all freed (`vsrc:src`, own mappings) |

- All three cells complete 100/100 and match B, the tunable run: medians within 0.1 MB, and no steps.
- The restart-producer cell is the product fix's test: there the long-lived peer is the consumer, and its frame-sized buffers come only from FrameLink's pool.
- **Sampler:** in the first two C runs the sampler found no long-lived peer, because it was given a relative bench path and matches children by their absolute one. Fixed (`realpath`) before the third run. The per-cycle values come from the bench summary in every run.
- **Not established:** the cells at the final revision (M6.3 qualification). The recorded `e1284833` failures stand.
- Files: `runs/*-C.summary.txt`, `analysis-fixed.txt`; raw runs in `fixed-91053ff9.tar.xz` outside the repository (SHA-256 in `fixed-91053ff9.tar.xz.sha256`).

## Files

- `smaps_sampler.py`, `bigalloc_trace.c` (build: `cc -O1 -g -shared -fPIC -o bigalloc_trace.so bigalloc_trace.c -ldl`), `run_cells.sh`, `analyze.py`.
- `analysis.txt`: `analyze.py` on all six runs. `per-process.txt`: frame-sized allocations per process.
- `stack-producer-videotestsrc.txt` and `stack-consumer-cpu-output.txt`: one backtrace of each kind. The plugin frames resolve with `addr2line` to `srcCpuOutputBuffer` and `srcSharedCpuStart`.
- `runs/`: the bench summary of each run, with the long-lived peer's per-cycle series.
- Outside the repository: `runs.tar.xz` (176 KB), with all samples, allocation logs and bench output, and its SHA-256 in `runs.tar.xz.sha256`.
