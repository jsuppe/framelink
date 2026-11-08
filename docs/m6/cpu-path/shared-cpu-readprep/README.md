# Consumer read preparation and packed-output initialization

Baseline450s instrumented BGRA4K30:13500video21600000PCM,2startup34msmisses. First two consumer copies each cause507thread minorfaults; firstcopy29.071ms. Later selected slowcopies mostly0faults. Counters are diagnostic, CPU accounting has coarse granularity; old15miss/1800s negative remains authoritative evidence of variability.

Matched10s premap-only: faults0, still2/300timingmisses. Next10s premap+skipredundantzero for tightly packed BGRA:0/300misses. No single favorable probe establishes qualification.

Production bounded change: consumer touches active bytes in retained read mapping before exporting pool, bounded by setup timeout. Source skips zero initialization ONLY when all output planes are contiguous/tightly packed and exactly cover allocation; padding/offset cases keep full initialization. Ownership/copy accounting/pixel semantics unchanged. No nativeGPU/Windows path change.

Linux17/17 affectedtests passed. Androidsource/channel/reconnect/roundtrip/AV/events6runtime pass; dualABIbuilds pass. Production5s BGRA4Kpreflight150video240000PCM FAILED1/150 atindex1:pushlateness16.907ms,arrival40.608ms,residual23.701ms. Medianarrival20.694ms. No waiver. This differs from original consumer first-read fault signature; next source-only control removes transport to examine producer scheduling/allocation. No performance closure claimed.

Preflight base555d366 plus two uncommitted sources (hashes in validation.json), binaryf8a42651. Commit containing this note freezes source. Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/android-4k-stage-probe. Diagnostic-only edits restored before production build. Final endurance remains pending on new revision.
