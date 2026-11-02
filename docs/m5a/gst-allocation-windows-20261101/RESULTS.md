# Windows explicit-completion ALLOCATION pool

Windows affected suites 5/5; Linux 3/3; Android native/pool/timing executables passed;
arm64/x86_64 builds pass. Native pool is Windows only.

The actual appsrc/native sink fixture obtains its buffers through a queried
GstBufferPool, not the action shortcut. Missing/unknown completion contract and
wrong geometry queries reject. Pool min=0/max=2, third DONTWAIT acquisition fails,
returned unsubmitted wrapper allows a fresh reservation. Original texture pixels
and metadata arrive correctly. Foreign memory rejects. After sink stop the old
pool rejects acquisitions, while a retained pooled buffer stays alive until release.
Internal producer test releases final GstMemory on another thread while acquiring;
completion is observed within 500ms of a controlled 20ms hold rather than waiting
for the 1000ms acquisition timeout. This is a liveness control, not a latency SLA.

No generic decoder, other-platform native pool, authenticated remote memory-policy,
throughput or physical-output claim. Caller completion contract is explicit.
