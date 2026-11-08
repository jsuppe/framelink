# Bounded shared-CPU output allocation reuse

Android memory control:7200/7200 full-byte checks pass. Interleaved1800
samples per mode, BGRA1080 size. Median allocation+zero+rowcopy8.856ms,
allocation+rowcopy8.945ms, allocation+contiguous8.816ms, reused+contiguous
2.108ms. This supports allocation/page-touch cost, not redundant zero or
row-loop cost as the dominant mechanism. Not a GStreamer timing gate.
Raw CSV retained at <linux-home>/dev/evidence/m6-cpu-path/android-copy-control.

The existing Windows two-buffer nonblocking output cache is generalized for
explicit POSIX shared CPU output. Both allocations are materialized before
track readiness. Output remains a copy; full padding zeroing stays unchanged.
Two cached buffers only; retained downstream buffers force uncached fallback,
never overwrite or wait. Stop/size change releases the cache without invalidating
outstanding buffers. Windows staging/size-change semantics preserved.

Regression extends paddedBGRA/NV12 to retain three outputs through stop,
covering both cached and fallback ownership. Linux4/4 focused tests pass;
Android source/A-V/events passed before warmup, source/A-V pass after warmup;
arm64 and x86_64 builds pass. Windows rebuild and2/2 tests pass.
Loom briefing unavailable (chromadb missing); ledger constraints used directly.

Unwarmed cache BGRA preflight improves CPU97.36% to36.10% and p9510.203ms
but retains4startup misses. Pre-materialized cache BGRA retains3startup misses,
max28.778ms,p9510.024ms: NOT qualified. NV124K30 pre-materialized cache passes
150video/240000PCM,zero34msmisses,max31.903ms,p9510.830ms. No startup exclusion,
threshold change, or clean-repeat waiver. New sustained validation required;
previous Linux4/4 andAndroidNV121080p60 results are pre-cache revision evidence.
