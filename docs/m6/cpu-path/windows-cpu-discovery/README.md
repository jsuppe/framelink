# Typed CPU control discovery prerequisite

FAR3 is an explicit CPU-control loopback record, using the existing current-user-only file/ACL and OS lifetime owner lock. FAR1 TCP and FAR2 Unix bytes remain unchanged. Legacy readers reject FAR3; CPU-control readers reject FAR1/FAR2. This internal runtime Endpoint structure is not the public C API.

Windows 3/3 and Linux 3/3 focused regressions pass (rendezvous, authentication, session transport). Tests cover exact record, cross-profile rejection, mixed Unix/CPU rejection, zero port/key, truncation/trailing bytes/unknown tag, unchanged failed outputs, owner exclusion and 100 publish/close cycles. Existing owner-crash and access tests retained; Windows NULL DACL rejects CPU reader/publisher too.

No local CPU A/V composition or rate qualification claimed yet. Loom briefing unavailable: chromadb missing. Full integrated 3f45f7b gate collected separately, 131/131 passed.
