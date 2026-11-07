# Primary zero-added-delay results — 2026-11-07

## Status: measurements complete for 14 supported cells; acceptance remains open

All selected untraced1800s captures have independent expected video/PCM counts,180000 contiguous audio blocks, exact common inferred A/V offset, zero arrivals over frozen17ms(60fps)/34ms(30fps), and each process below the fixed64KB/min memory-growth limit. These arrival diagnostics include source/copy/consumer work; they are NOT isolated per-hop overhead budgets or physical output timing. They do not erase historical failures.

Pool2/unread1/output1,latency0,transport-only,sinks sync=false/async=false. Runtime revisions/binary hashes and raw hashes are in each linked capture. Later Windows-only cache changes do not alter Linux/Android production paths. Native source means allocation wrapping, not proof of consumer GPU work or zero-copy producer. Linux/Android memory is RSS; Windows is privatecommit.

| Platform / cell | Frames | Arrival p95 / max ms | Consumer / producer KB/min | Evidence |
|---|---:|---:|---:|---|
| Linux BGRA1080p60 native | 108000 | 8.083 / 14.953 | 3.23 / 3.23 | [capture](zero-delay-platform-controls/linux-bgra1080p60-current) |
| Linux NV121080p60 native | 108000 | 7.421 / 10.943 | 3.23 / 3.24 | [capture](zero-delay-platform-controls/linux-nv121080p60-current) |
| Linux BGRA4K30 native | 54000 | 13.480 / 17.730 | 3.23 / 3.23 | [capture](linux-mapping-repair/sustained) |
| Linux NV124K30 native | 54000 | 6.674 / 8.934 | 3.23 / 3.24 | [capture](zero-delay-platform-controls/linux-nv124k30-current) |
| Android RGBA1080p60 native | 108000 | 7.657 / 10.813 | -0.00 / 0.00 | [capture](zero-delay-platform-controls/android-1080p60-current) |
| Android RGBA4K30 native | 54000 | 11.995 / 23.476 | -2.30 / -2.92 | [capture](zero-delay-platform-controls/android-4k30-soak) |
| Windows BGRA 1920x1080@60 native | 108000 | 5.780 / 10.540 | 7.14 / 3.24 | [capture](zero-delay-platform-controls/windows-bgra-1920x1080-60-native-1800) |
| Windows BGRA 1920x1080@60 CPU | 108000 | 6.530 / 10.139 | 7.18 / 7.22 | [capture](zero-delay-platform-controls/cpu-pool-windows-bgra-1920x1080-60-cpu-1800) |
| Windows BGRA 3840x2160@30 native | 54000 | 13.556 / 15.769 | 6.93 / 6.52 | [capture](zero-delay-platform-controls/layout-cache-windows-bgra-3840x2160-30-native-1800) |
| Windows BGRA 3840x2160@30 CPU | 54000 | 15.757 / 19.367 | 7.14 / 7.18 | [capture](zero-delay-platform-controls/cpu-pool-windows-bgra-3840x2160-30-cpu-1800) |
| Windows NV12 1920x1080@60 native | 108000 | 4.933 / 9.582 | 7.69 / 5.68 | [capture](zero-delay-platform-controls/windows-nv12-1920x1080-60-native-1800) |
| Windows NV12 1920x1080@60 CPU | 108000 | 5.969 / 7.883 | 3.25 / 7.34 | [capture](zero-delay-platform-controls/cpu-pool-windows-nv12-1920x1080-60-cpu-1800) |
| Windows NV12 3840x2160@30 native | 54000 | 8.495 / 10.324 | 6.96 / 45.72 | [capture](zero-delay-platform-controls/layout-cache-windows-nv12-3840x2160-30-native-1800) |
| Windows NV12 3840x2160@30 CPU | 54000 | 12.653 / 15.066 | 7.32 / 46.69 | [capture](zero-delay-platform-controls/cpu-pool-windows-nv12-3840x2160-30-cpu-1800) |

## Queue distributions

Exact paired output-queue admission/residence distributions, including startup/drain, are included per cell below and in PRIMARY_RESULTS.json. Sampled queue occupancy is not an exact peak. These distributions cover benchmark output queues, not physical scanout or all internal transport wait stages.

- Linux BGRA1080p60 native: video `n=108000 p50=0.04 p95=0.05 p99=0.06 max=0.77 min=0.00`; audio `n=180000 p50=0.04 p95=0.06 p99=0.07 max=0.98 min=0.01`.
- Linux NV121080p60 native: video `n=108000 p50=0.09 p95=0.10 p99=0.11 max=1.48 min=0.01`; audio `n=180000 p50=0.09 p95=0.10 p99=0.10 max=1.56 min=0.00`.
- Linux BGRA4K30 native: video `n=54000 p50=0.04 p95=0.05 p99=0.07 max=0.89 min=0.01`; audio `n=180000 p50=0.04 p95=0.05 p99=0.07 max=3.10 min=0.00`.
- Linux NV124K30 native: video `n=54000 p50=0.09 p95=0.11 p99=0.12 max=1.83 min=0.01`; audio `n=180000 p50=0.09 p95=0.12 p99=0.13 max=1.63 min=0.00`.
- Android RGBA1080p60 native: video `n=108000 p50=0.34 p95=0.50 p99=0.59 max=2.37 min=0.05`; audio `n=180000 p50=0.26 p95=0.48 p99=0.59 max=5.06 min=0.06`.
- Android RGBA4K30 native: video `n=54000 p50=0.49 p95=0.68 p99=0.80 max=3.68 min=0.10`; audio `n=180000 p50=0.34 p95=0.62 p99=0.71 max=2.49 min=0.06`.
- Windows BGRA 1920x1080@60 native: video `n=108000 p50=0.01 p95=0.01 p99=0.01 max=0.06 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=1.32 min=0.00`.
- Windows BGRA 1920x1080@60 CPU: video `n=108000 p50=0.01 p95=0.01 p99=0.01 max=0.04 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=0.76 min=0.00`.
- Windows BGRA 3840x2160@30 native: video `n=54000 p50=0.01 p95=0.01 p99=0.01 max=0.11 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=0.98 min=0.00`.
- Windows BGRA 3840x2160@30 CPU: video `n=54000 p50=0.01 p95=0.01 p99=0.01 max=0.05 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=0.65 min=0.00`.
- Windows NV12 1920x1080@60 native: video `n=108000 p50=0.01 p95=0.01 p99=0.01 max=0.34 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=2.29 min=0.00`.
- Windows NV12 1920x1080@60 CPU: video `n=108000 p50=0.01 p95=0.01 p99=0.01 max=0.56 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=1.16 min=0.00`.
- Windows NV12 3840x2160@30 native: video `n=54000 p50=0.01 p95=0.01 p99=0.01 max=0.14 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=0.59 min=0.00`.
- Windows NV12 3840x2160@30 CPU: video `n=54000 p50=0.01 p95=0.01 p99=0.01 max=0.59 min=0.00`; audio `n=180000 p50=0.01 p95=0.01 p99=0.01 max=2.85 min=0.00`.

## Retained failures and limits

- Linux BGRA4K30 originally165/54000 arrivals over34ms,max43.64ms. Matched fault/write-interval controls supported the mapping-lifetime repair; repaired sustained run passed. See linux-mapping-repair.
- Windows native NV124K30 originally6/54000 over34ms,max159.06ms. Two traced reproductions and repaired trace-off runs were clean; no causal repair of that intermittent event is proven. This remains an unresolved acceptance issue owned by Evelyn, not waived by current passes.
- Android original60s control had2 arrivals over17ms,max19.10ms; later current-profile1800s was clean. Historical tails remain recorded; no universal real-time guarantee.
- Three Windows resource cells originally exceeded64KB/min. Native layout probe repair and bounded CPU cache now pass matched sustained gates; precise historical CPU retained-growth cause is not proven. RESOURCE_GATES.md retains all rows.
- Linux CPU target cells remain unsupported/deferred toM6.8; Android CPU target cells remain unsupported/deferred toM6.8. They are not silently counted as passes. Android native NV12/BGRA exact layouts reject rather than masquerade as supported.
- Host load was not hermetically controlled; existing captured logs/hash/environment evidence is retained. No retrospective GPU-load estimate is invented.

## Integrated regressions

Linux114/114 on0ff18eb; Windows112/112 on production29bbddb plus final deterministic-control1/1; Android72/72 on integrated source, arm64 runtime and both arm64/x86_64 builds. No failures/skips. See final-regressions, queue-controls and zero-delay-platform-controls/windows-cpu-pool.

## Acceptance decision still required

M6.1 numeric latency/CPU/GPU budgets were derived for150ms buffered playback. They cannot automatically approve this primary transport profile. D5 proposes isolated added-hop p95<=2ms, but current whole-path arrival measurements do not measure that quantity. Keep17/34ms as diagnostics and64KB/min as the existing hard resource gate; do not tune limits to current results. Jon owns product budget acceptance; Evelyn owns measurement isolation and investigation of retained timing failures. No permission to waive either is inferred.

M6.2b/2e therefore remain open;2f publication is complete. Suspended acceptance is explicit, not milestone completion. Continue ready lifecycle/resource work insideM6 sequentially while preserving these blockers; M6 cannot close until they are resolved andM6.3–9 are complete.
