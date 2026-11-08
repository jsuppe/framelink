# Android BGRA4K matched affinity diagnostic

Question: does restricting the identical diagnostic transport workload to the two high-capacity CPU cores alter consumer copy CPU/offCPU cost and late-frame rate? This is not a proposed production affinity policy or a qualification run.

Device exposes CPU0–5 capacity516/1.8GHz and CPU6–7 capacity1024/2.0GHz. Use existing all-frame stage instrumented binary 0f1e89a4 from source93283ac; production dee5c211 remains untouched. Change only inherited process/thread CPU mask: A=ff (0–7), B=c0 (6–7). Fixed ABBA order,120s each,3600 video/5760000 PCM/12000 audio blocks each. Retain startup/drain, every late frame, zero/fallback counts and all stage rows. Check observed per-thread masks after2s. This process-wide control can introduce big-core contention; it cannot isolate a particular thread or prove a scheduler defect. Chronological/thermal confounding remains; ABBA only partially controls phase drift.

5s preflights validate instrumentation/counts/effective masks, not acceptance. Big-core preflight retains4/150 misses max37.738822ms, so affinity is not established as a remedy. No clean-repeat waiver. Do not change budgets/ownership or launch a production endurance rerun based merely on this control.

Evidence: <linux-home>/dev/evidence/m6-cpu-path/android-residual-audit/affinity-control. Sole systemd campaign executes the four cells strictly sequentially. Next collect campaign/status, exact counts, exhaustive stage coverage and compare copy wall/thread CPU/offCPU distributions and missed-frame neighbors. NV121080 failed production result remains open; no cross-format inference.
