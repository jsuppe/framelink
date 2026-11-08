# Remaining Android BGRA4K transport residual

Production93283ac input-retention preflight delivers all150video/240000PCM but fails34ms on frames27–29, max37.353243ms. Source pushes all<0.734ms late. Do not repeat until a clean pass or relabel as qualified. The completed source-allocation fix remains supported by its independent zero-fault control; next investigate the later post-push residual.

Temporary all-frame diagnostic logs sink release wait/acquire/copy/submit; consumer receive/output acquisition/copy/release/metadata; consumer copy thread faults/CPU; nonblocking output-cache fallbacks. Source allocation/fill probes retained. Source and sink stages are 1-based and0-based respectively; join by consumer seq-1, and preserve all raw push/arrival rows. A long receive interval often includes ordinary cadence; it is not itself an added latency or causal diagnosis. Logging is diagnostic overhead, not trace-off qualification.

5s coverage preflight:150video/240000PCM,500audio blocks; exactly150consumer and150sink stage rows,0fallbacks, all34ms timing passes. This validates instrumentation only. Next one450s sample expects13500video/21600000PCM/45000audio and exactly13500stage rows per side. Use all missed frame indices and neighboring producer/consumer stages; distinguish copy CPU cost, scheduler delay, fallback allocation, and lease waiting before making another change. No threshold waiver or unsupported hardware attribution.

Evidence: <linux-home>/dev/evidence/m6-cpu-path/android-residual-audit. Diagnostic binary0f1e89a4; production source restored93283ac. Final rate qualification remains open.
