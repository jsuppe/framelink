# Windows primary-profile resource gate reconciliation

The four CPU sustained cases all pass complete video/PCM counts and the frozen 17/34ms arrival diagnostic. This does not mean all acceptance gates passed. The existing QUALIFICATION_MATRIX.md fixed memory slope limit is 64 KB/min per process for untraced runs >=10 minutes; it is not waived by the open primary-profile latency-budget decision. Positive slopes below this limit are not automatically failures.

| Case | Consumer KB/min | Producer KB/min | Memory gate |
|---|---:|---:|---|
| scratch-off-windows-bgra-1920x1080-60-cpu-1800 | 21.19 | 6.49 | pass |
| scratch-off-windows-bgra-3840x2160-30-cpu-1800 | 98.11 | 7.50 | FAIL |
| scratch-off-windows-nv12-1920x1080-60-cpu-1800 | -11.61 | 6.91 | pass |
| scratch-off-windows-nv12-3840x2160-30-cpu-1800 | -154.28 | 46.98 | pass |
| scratch-off-windows-nv12-3840x2160-30-native-1800 | 69.28 | 47.13 | FAIL |
| windows-bgra-1920x1080-60-native-1800 | 7.14 | 3.24 | pass |
| windows-bgra-3840x2160-30-native-1800 | 69.47 | 7.40 | FAIL |
| windows-nv12-1920x1080-60-native-1800 | 7.69 | 5.68 | pass |
| windows-nv12-3840x2160-30-native-1800 | 69.70 | -60.65 | FAIL |

Before the layout-cache repair, three selected 4K cases exceeded the memory gate: BGRA native 69.47, scratch-reuse NV12 native 69.28, and BGRA CPU 98.11 KB/min. Original pre-scratch NV12 native also exceeded it (69.70) and had the separate 159ms timing failure. Traced runs are not used to pass this untraced gate.

Consumer sample inspection: native NV12 private commit median rises from 102744 KB at 900–1200s to 103448 KB at 1500–1799s. BGRA CPU alternates approximately 143 MB and 176 MB, so its fitted slope can reflect occupancy as well as retained allocations. Neither observation proves a leak or a plateau. Do not relabel this as passing or defer acceptance entirely to M6.5.

Next discriminating baseline: same Windows binaries, 4K30 NV12 live video plus 48kHz audio, same 1800s duration, source-only (no FrameLink transport/consumer). Measure common GStreamer source + sampler private commit against the unchanged 64KB/min gate. This cannot directly exonerate the consumer: a clean baseline narrows investigation to consumer/transport work; a rising baseline identifies shared harness/source costs worth isolating. No repeat of the already-clean timing soak is needed. Parent owns attribution; original rare timing failure remains separate.

M6.2 remains open for resource attribution, deterministic controls, primary-profile budgets and retained timing failures; M6.3–M6.9 remain open.

## Source baseline result

Source-only NV124K30 completed1800s with7.36KB/min private commit growth, flat handles/threads,CPU5.12%. See [source baseline](windows-source-resource/README.md). Next isolate the consumer harness using `--role consumer --local-consumer --result FILE`; reject native/element-mode combinations and require independent video/audio counts. This control never qualifies FrameLink transport.

Local-consumer control completed1800s with54000video/86400000PCM, no diagnostic misses,7.34KB/min growth. [Native-layout diagnostic](windows-native-layout/README.md) isolates per-frame temporary texture-layout probing: after10000warm-up frames,50000more flushed probes added1588KB versus0KB using cached measured layout. Fixed-size per-slot metadata cache implemented; exact generation/texture identity invalidate it and leases/fences remain unchanged. Full regression and sustained rerun must pass before clearing native resource failures. BGRA CPU growth and original159ms stall remain open.

## Layout-cache NV12 sustained result

[Native NV124K30 repaired1800s](layout-cache-windows-nv12-3840x2160-30-native-1800/README.md): complete54000video/86400000PCM, zero34ms misses, AVmapping0ns. Consumer6.96KB/min and producer45.72KB/min pass64KB/min (previous consumer69.28). Full Windows regression112/112 passed. Native NV12 resource failure is repaired at this revision; original timing failure is not causally closed. Next validate the same cache in native BGRA4K30; BGRA CPU remains separate. Historical failing rows remain evidence, not current repaired measurements.

## Layout-cache BGRA sustained result

[Native BGRA4K30 repaired1800s](layout-cache-windows-bgra-3840x2160-30-native-1800/README.md):54000video/86400000PCM, zero34ms misses, AVmapping0ns; consumer6.93KB/min (previous69.47),producer6.52KB/min. Both native4K resource cases now pass after the cache repair. BGRA CPU98.11KB/min remains unresolved. Next discriminating control: BGRA4K30 local-consumer1800s with identical queue/appsink/count recording but no transport or GPU readback, comparing against the previous NV12 local-consumer7.34KB/min. This separates format-dependent harness/source allocations from the CPU transport/readback path; no transport qualification claim.

BGRA local-consumer baseline completed with7.31KB/min (NV12local7.34), complete54000video/86400000PCM and zero diagnostic misses. See windows-bgra-local-consumer-resource. Next isolated GstBuffer allocate/reuse probe tests33MB per-frame allocation pressure; CPU-path fitted98.11KB/min remains failing pending attribution/repair, not excused as sampling noise.

## CPU output-cache BGRA sustained result

[BGRA CPU4K30 repaired1800s](cpu-pool-windows-bgra-3840x2160-30-cpu-1800/README.md): complete54000video/86400000PCM, zero34ms misses, AVmapping0ns. Consumer7.14KB/min (previous98.11),producer7.18 pass64KB/min. All three originally failing selected resource cells now have repaired-revision passing evidence. CPU allocation churn is proven; exact retained-growth cause is not. Next sustained regressions cover other CPU formats/rates sharing the new cache; native159ms stall, deterministic queue controls and primary-profile budgets remain open.

CPU-cache NV12CPU1080p60 sustained regression passed108000video/86400000PCM, zero17ms misses/AVmapping0ns; consumer3.25/producer7.34KBmin below64. See cpu-pool-windows-nv12-1920x1080-60-cpu-1800. Current cache CPU matrix2/4; BGRA1080p60 thenNV124K30 remain.

CPU-cache BGRA CPU1080p60 sustained regression passed108000video/86400000PCM, zero17ms misses/AVmapping0ns; consumer7.18/producer7.22KBmin below64. See cpu-pool-windows-bgra-1920x1080-60-cpu-1800. Current cache CPU matrix3/4; finalNV124K30 remains.
