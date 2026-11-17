# Linux timing controls — 2026-11-04

Benchmark/harness change following 9d52ee5, no production default changes.
Linux local RTX3090 host; BGRA1920x1080@60, native consumer, separate processes.
Each run5seconds (2second warmup), 300 video frames and240000 stereo PCM
sample-frames. All completed with clean EOS. These are short controls, not
30-minute qualification or physical output, and producer copies remain.

Reproduce with fl_gst_av_bench <plugin> --seconds 5 --size 1920x1080 --fps 60
--format BGRA --native --out <directory> --label <name>, plus:
- zero-bin: --pool 2 --video-queue 1 --latency-ms 0 --arrival-budget-ms 17 --transport-only
- zero-element: same plus --mode element
- buffered: --latency-ms 150 --arrival-budget-ms 150

17ms is an explicit diagnostic arrival budget, not the proposed incremental
hop p95 acceptance target. All three have zero recorded deadline misses.
Buffered control video delivery p95=150.16ms, versus8.08ms for zero-bin;
source arrival p95 stays near8ms. This isolates intentional output scheduling
from arrival cost. No incremental second-hop measurement is claimed.

Regression: rebuilt gst_av_parity passed (1 CTest invocation,85.94seconds).
Saved summaries contain requested settings and counts. Output queue capacity
remains1second; occupancy instrumentation and hard low-latency queue profiles
are still pending. An exit0 proves completeness, not all performance budgets.
