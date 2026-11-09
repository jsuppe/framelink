# A/V bin explicit video overload controls

Additive `buffer-wait-ms` (0..1000, default5) and `error-on-ring-full`
(defaultfalse) on `framelinkavsink` forward to its video child. Selection survives
track-driven child reconstruction. Existing state/policy binding immutability
applies. No default, native C structure, wire format or fallback change.
PCM and strict shared-CPU/native ownership semantics are not relaxed.

Validation: **Windows4/4, Linux3/3, Android3/3**, plus Android arm64/x86_64
builds. Exact commands, source hashes and raw logs retained here. Extended
pool-policy tests hold every actual native read lease in 2/3-slot rings and
exercise bin drop-and-count, bounded error, and delayed-reader recovery. They
check exact received/submitted/dropped counts, error class and wait bounds,
plus unchanged defaults, child reconstruction and state immutability.
Native producer with PCM, existing shared CPU A/V and bin lifecycle/negative
regressions also pass. These are focused shared-change checks, not deferred
performance campaigns. Loom briefing unavailable (chromadb missing).

Public overload-profile decision D2 is unresolved. This exposes existing
caller controls without silently deciding a product-wide default. D3 remains
open; API not frozen, M6 open, M7 hold. Clean exact-source Windows integrated
build/API verification is the next owned command.
