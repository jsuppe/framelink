# Windows bin CPU-mapped native-transport handle diagnostic

Base `3ca89e54e64d7a0baf8ba52e4c12ee0cdfd9c6f0`, <win-host>. Temporary remote test-only instrumentation; production DLL unchanged. Build/test exit 0, all 100 unique cycles collected. This is not shared-CPU transport qualification or an integrated-suite pass.

First sample 2544 handles, second 1557, plateau 1567 at cycles 3–32, final 1524. Registry keys fall from 2193 to 1194. Early plateau includes increases in Thread/Event/IoCompletion objects, followed by later releases; File count 34 -> 33 and Section 13 -> 11. No evidence here of unbounded per-cycle retained frame allocations. Cold first-sample maximum masks the later initialization increase under the existing three-sample baseline rule; this diagnostic cannot explain or waive the original mode-ordered 1563 -> 1573 failure.

Next bounded diagnostic restores original element/bin CPU/native ordering and eight cycles with per-cycle type counts, including deliberate leak control. No threshold or production change yet. Owner Evelyn; lifecycle acceptance remains unresolved. M6 open, M7 held.

## Original-order control collected

All 40 cycles were collected in the original four-mode order plus leak control. Bin CPU handles 1557 -> 1561, bin native 1563 -> 1563; Thread/Event/IoCompletion account for the early bin increase, while File/Section counts stay fixed. Deliberate socket leakage increases File handles 49 -> 161 (112 = seven increments of 16). Total handle gate still detects the leak despite unrelated asynchronous background releases. This control did not reproduce the original failure and does not clear it. Raw data and exact instrumentation are in `order/`.

Next causal intervention: call `gst_task_cleanup_all()` after destroyed pipelines and before each handle sample, leaving mode order, cycle count and thresholds unchanged. This joins GStreamer task-pool cleanup rather than increasing warm-up counts or leak tolerance.

## Causal task-cache control and bounded fixture repair

With the only intervention `gst_task_cleanup_all()` after pipeline destruction, all eight CPU bin samples were 1532 and all native bin samples 1534; other ordinary modes were stable after registry initialization. The leak-control File handles still grew by 112, and the total-handle gate rejected it. Exact counters at the existing gate and diagnostic snapshots can differ slightly due to asynchronous background activity; both logs are retained. This isolates the observed transient worker-handle growth to the idle task cache, without claiming every earlier resource failure had that cause.

Canonical repair is Windows-only in `test/fl_gst_av_qualification.cpp`: join that idle task cache before the existing handle measurement. No threshold, cycle count, production API, or lifecycle ownership change. Full original qualification (including trace accounting and child configurations) passed, build/test exit 0; original integrated failure remains preserved. Unrelated Windows private-memory growth and native producer integration remain open.

Uninstrumented fixed full qualification passed all four cleanup cases, detected deliberate leakage, and passed the child A/V, native, video-only, audio-only, accounting and negative controls. Raw logs and test SHA256 are in `fixed-full-qualification/`. CPU-bin samples were exactly1549 across eight cycles; native-bin samples1553–1555 stayed within the unchanged limit. The original integrated failure remains a historical negative; a new exact-revision full integrated run is required.
