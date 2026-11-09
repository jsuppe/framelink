# Windows three-process producer-death controls

Base `1460444f4aa9368ad046ecfd06f3287bfee48e62`; status.json pins source overlay hashes.
The source, checked native/PCM relay and final reader run in separate processes.
Two fault cases kill the actual source process: raw native producer and GstMemory
reservation producer. The parent verifies termination exit code 73, not merely
an injected session flag. Source death occurs after one exact PCM block arrives
and while the final consumer holds a native read lease.

The relay must report a non-timeout event failure within the preselected 1.5s
bound (2s API timeout), reject further publication, keep exactly one pending
lease, and return BUSY from finish while that reader still owns the texture.
Only then does the reader verify every pixel and complete its lease. The relay
must then collect and finish successfully. The final consumer must receive an
explicit non-timeout failure, never a fabricated END or authenticated video EOS.
Both children are bounded/job-contained; the relay must exit0. Normal control
cases retain exact pixels, PCM, metadata, A/V mapping, terminal and reservation
checks. This is not recovery/reconnect, consumer-death, GPU-hang, automatic-bin
or target-rate qualification. No production API, defaults or implementation changed.

Initial diagnostic passed: raw source death observed in 11.548ms, GstMemory
source death in 9.723ms. These are diagnostic observations, not a transport
latency acceptance claim. **7/7 focused Windows regressions passed (30.39s)**, including the registered
30s-outer-timeout fault test. Results are in regressions.log.
Loom context attempted: unavailable because chromadb is missing.
All existing M6 gates and D2/D3 decisions remain open; M7 remains on hold.
