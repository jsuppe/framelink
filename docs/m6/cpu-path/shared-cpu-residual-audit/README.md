# Remaining Android BGRA4K transport residual

Production93283ac input-retention preflight delivers all150video/240000PCM but fails34ms on frames27–29, max37.353243ms. Source pushes all<0.734ms late. Do not repeat until a clean pass or relabel as qualified. The completed source-allocation fix remains supported by its independent zero-fault control; next investigate the later post-push residual.

Temporary all-frame diagnostic logs sink release wait/acquire/copy/submit; consumer receive/output acquisition/copy/release/metadata; consumer copy thread faults/CPU; nonblocking output-cache fallbacks. Source allocation/fill probes retained. Source and sink stages are 1-based and0-based respectively; join by consumer seq-1, and preserve all raw push/arrival rows. A long receive interval often includes ordinary cadence; it is not itself an added latency or causal diagnosis. Logging is diagnostic overhead, not trace-off qualification.

5s coverage preflight:150video/240000PCM,500audio blocks; exactly150consumer and150sink stage rows,0fallbacks, all34ms timing passes. This validates instrumentation only. Next one450s sample expects13500video/21600000PCM/45000audio and exactly13500stage rows per side. Use all missed frame indices and neighboring producer/consumer stages; distinguish copy CPU cost, scheduler delay, fallback allocation, and lease waiting before making another change. No threshold waiver or unsupported hardware attribution.

Evidence: <linux-home>/dev/evidence/m6-cpu-path/android-residual-audit. Diagnostic binary0f1e89a4; production source restored93283ac. Final rate qualification remains open.

## Collected 450s diagnostic (2026-11-08 18:35 UTC)

Exit/pull0, complete EOS, exactly13500video/21600000PCM at both endpoints,45000contiguous audio timing rows,13500sink and13500consumer stage records,0output-cache fallbacks. All-frame correlation retains5/13500 video misses (indices3078,4871,4883,9716,9717), max40.153828ms; audio0misses and common A/V mapping0ns. Diagnostic only, NOT qualification.

All missed frames and neighbors are preserved in stage-analysis.json. Miss-frame sink copies8.842–15.946ms and consumer copies18.136–28.513ms account for most residual. Consumer copy CPU12.807–20.290ms versus wall18.136–28.513ms indicates a mixture of CPU work and off-CPU delay, not proof of a specific scheduler mechanism. Miss-frame lease wait53–897us and output acquisition17–79us are small; next-frame wait can rise to6.420ms after the late consumer (backpressure consequence). No fallback allocations. Three misses have4–6 minor faults, two have0; first-touch faults cannot explain every miss. Do not remove ownership waits or claim hardware impossibility.

Source alloc/fill diagnostics intentionally cover early/selected events only (not13500rows); the initial validation assertion requiring all-frame source allocation coverage was corrected. Sink/consumer coverage and timing rows are exhaustive.

Disposition: BGRA4K acceptance SUSPENDED, owner Evelyn, pending a bounded matched copy-cost/off-CPU control that identifies a fixable mechanism; no clean-repeat waiver. No production change from this diagnostic. Advance the affected93283ac Android BGRA1080p60 production rate gate next, then remaining affected cells sequentially. Previous readprep Linux4/4 and AndroidNV122/2 remain evidence for the earlier revision, not the changed bin.
