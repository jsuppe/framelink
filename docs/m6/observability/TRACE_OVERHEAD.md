# M6.6b trace overhead and bounds

Three ordered trace-off/on pairs on each platform used300frames1080p60, one native hop, GPUverification and testGDP PCM. All counts and audio/media-offset gates pass; trace-on transport/GPU joins have zero loss. Windows explicitly copies/reads back whole frames; POSIX verification reads36bytes/frame. These are fixed diagnostics, not app/presentation qualification.

P95 arrival-age differences (on minus off): Linux -0.014..+0.052ms; Android -0.030..+0.100ms; Windows -0.478..+0.005ms. Process CPU deltas and peakRSS/working-set values are retained in each platform overhead-summary.json. Short ordered pairs show substantial noise: no precise universal CPU percentage or improved-performance claim. Explicit flush cost is separate; automatic-exit flush is excluded. Traced performance does not replace trace-off gates.

Measured64-bit Event size168bytes. The old65536 default could reserve10.5MiB/thread,672MiB at64threads per recorder. New4096 default reserves672KiB/thread,42MiB at64threads (plus allocator/container overhead). Tracing remains disabled by default with no event allocations. Explicit16..1048576 overrides remain available; highest configuration potentially10.5GiB per recorder is opt-in, not a recommended blanket size. Separate namespaces/processes multiply bounds. RSS is not reserved capacity.

Linux/Android/Windows each passed8 targeted tests: disabled, defaultcapacity, overridecapacity, malformedcapacityfallback, boundedconcurrentoverflow, threadlimit, details, compiledout. Default/override tests emit one excess event and require exactly one dropped event; 4threads x128 events at16slots prove448dropped;65threads prove1rejected. Full-run analysis rejects any nonzero loss. Existing15test baseline evidence retained. Windows preflight CMake difference was preserved, not overwritten: only three new trace tests applied.

Explicitly sized prior captures retain their actual settings; they are not relabeled as using the new default. M6.6a integrated-current-renderer evidence remains pending M6.9, and final integrated test gates remain M6.7c/9e.
