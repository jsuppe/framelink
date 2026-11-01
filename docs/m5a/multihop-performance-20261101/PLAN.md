# M5a.13 — pre-measurement gates and execution order

Task12 functional qualification is complete within documented supported paths.
One parent executor, sequential execution. This task remains OPEN.

1. Validate cross-process trace joins on the already-qualified 16-frame GPU relay.
   Mandatory: three process files, zero recorder loss, exactly16 upstream and16
   downstream successful submit/acquire identity joins, no unmatched/ambiguous
   generation/fence/PTS tuples. Confirm same-host steady-clock timestamps. This
   small handshake-driven fixture is instrumentation validation, NOT throughput.
2. Implement matched one-boundary/two-boundary workload with independent paced
   producer, two allocations and no per-frame test-control handshakes. Preserve
   identities, timestamps and complete PCM on a common timeline. Warm up separately.
3. Predeclare baseline hardware/workload and budgets: 1080p60 BGRA/RGBA and NV12
   where native format supported, 60 measured seconds after startup;3600 identities,
   zero unaccounted drops/repeats, zero hidden relay copy/map/upload/readback, no
   forced pool-fill latency. Unsupported formats must reject, not count as pass.
   Throughput gate >=59.4 fresh frames/sec; report all drops even if rate passes.
   Compare two-boundary minus one-boundary p95 delivery residence <=one frame
   period (16.667ms); report p50/p95/p99/max, occupancy and backpressure separately.
   If a device cannot meet the declared workload, record failed gate/capability
   limit rather than silently reducing workload or increasing the pool.
4. Run trace-disabled then trace-enabled matched cases on each supported host.
   Enabled instrumentation must not create drops; compare steady throughput within
   2% and p95 delivery residence within one frame period. Flush excluded from media
   timing but reported separately. Recorder loss or ambiguous joins invalidates run.
5. Inject delayed final reader; verify safe reuse and bounded occupancy, report
   expected backpressure/drop policy separately from nominal rate. Validate A/V
   offset preservation and arrival skew independently; physical output excluded.
6. Publish evidence and limitations. Numeric baseline budgets are acceptance
   targets, not promises that unmeasured hardware already meets them.

Current next action: instrumentation-validity capture on Linux GPU relay.
