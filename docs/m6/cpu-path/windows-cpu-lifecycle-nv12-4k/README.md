# Shared-CPU production lifecycle — NV124K30

Frozen461584b production DLLs, testa5d52d1 benchmark guard only. Native D3D
lifecycle failures remain suspended. Same exact host/adapter/software environment.

Persistent consumer/fresh producer:2x5s control then100x5s PASS23:50:38Z.
All100cycles exact150video/240000PCM each,15000video/24000000PCM total.
Original five warmups and16KiB/.1handle/.02thread per-cycle thresholds unchanged.
Slopes:-2.397816KiB,-.149720handles,-.069835threads per cycle. Independent raw
counts/slope checks,126remote hashes/fullcoverage,unchanged3DLL pins and idle0 verified.
No per-frame timing, pixels, native ownership or physical-output claim from this test.

Original binaries/full raw evidence: <linux-home>/dev/evidence/m6-lifecycle/shared-cpu-nv12-4k-producer-461584b-harness-fix.
Binaries excluded from repo; committed text is localenv-normalized, original hashes
refer to external bytes. Both roles are now collected passes. M6open/M7held.

Persistent producer/fresh consumer:2x5s control plus100x5s PASS23:59:50Z. Exact15000video/24000000PCM; original limits retained. Slopes and independent126-hash/fullcoverage/idle proof: persistent-producer/data/status.json and persistent-producer/independent-collection.json.
