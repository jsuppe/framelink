# Resource and control-thread flush observations

Three sequential two-hop300-frame1080p60/pool2 cases per host: video only untraced,
A/V untraced, A/V traced. RGBA on Linux/Android; NV12 on Windows. All nine delivered
300/300; audio cases retained240000 stereo PCM sample-frames and exact37ms media
offset. Traced cases account600 joins/300 GPU completions with zero recorder loss.
These short runs measure process costs, not sustained FPS (separate62s campaigns)
or long-duration leak/soak behavior. There was no new numeric CPU/RSS pass threshold.

| Host | Video CPU seconds | A/V CPU seconds | Traced A/V CPU seconds | Explicit flush per process ms |
|---|---:|---:|---:|---|
| Linux RTX3090 |2.154308|2.275998|2.711736|1.771–9.162|
| Android TCL8483A |2.977670|3.869326|3.899507|19.781–53.069|
| Windows RTX5090 |0.890625|5.390625|5.265625|2.918–6.321|

CPU sums are three process CPU times before flush, including initialization,
GStreamer plugin loading, generation, verification and teardown. They are NOT
steady-state audio memcpy CPU measurements or statistical overhead estimates.
Windows extra total CPU is visible, not explained away: attribution and repeated
steady-state/resource soak qualification belong to M6. Full A/V rate already passed.

Sum of per-process peak resident sizes (MiB), video/A/V/traced A/V:
Linux207.781/224.816/224.965; Android165.215/169.938/171.574;
Windows123.941/198.492/199.770. These peaks need not coincide and shared pages may
be counted in multiple processes; the sums are NOT unique physical memory usage.
GPU memory/device residency and uncommitted recorder virtual allocations are not
measured by RSS/working set. Detailed per-process JSON retained.

Control-thread flTraceFlush runs after media processing and native cleanup. Windows
also flushes the separate executable completion recorder. The duration is reported
outside media timing; existing automatic process-exit flush may run again and is
explicitly NOT included. It is not safe to infer the whole exit wall time from this
number. Disabled calls are no-op control costs, not per-frame instrumentation cost.

Source adds only a main-scope end-of-run reporter; no frame-path hook or production
transport change. Linux/Android getrusage; Windows GetProcessTimes/PeakWorkingSetSize.
Builds passed Linux, Windows, Android arm64/x86_64; runtime Android arm64 only.
Windows resource EXE hashAF590AC821BC036D9F67916494FF2B363A7BFDDB7A3A3E44CA39B93B11110F7D;
DLL unchanged2F07503E...; full hashes in run logs. Linux/Android hashes included.

Reproduce: FL_BENCH_FRAMES=300 FL_BENCH_WARMUP=60 FL_BENCH_HOPS=2,
FL_BENCH_GPU=1 (Linux/Android), FL_BENCH_FORMAT=NV12 (Windows),
FL_BENCH_AUDIO=0/1; trace disabled or enabled with1048576 slots. Three *.resources-ROLE.json
files must exist. Run existing video/audio/trace analyzers with --frames300 and
video --warmup60 (normal spaces). Exactly three distinct roles, positive CPU/RSS,
all identity/PTS/sample/completion checks and zero trace loss were verified.
