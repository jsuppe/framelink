# Windows coordinated native receiver A/V

Base `6d7e6be7a6a1f6acb3dcdaea0a4c98a55456d985`, exact test overlays hashed in status.json. Windows focused regression passed (13.22 seconds).

Four additional broker-bound cases use D3D11 producer and receiver memory with an authenticated zero-operation policy, deferred reader leases and an explicit retain-memory-until-GPU-complete allocation contract. Exact pixels (explicit test-only readback respecting pitch), PCM bytes, 37ms mapping, DTS/duration, both EOS orders, empty EOS and audio endpoint failure are checked. Final producer and receiver memory survive complete pipeline/session destruction with a usable device. The first missing-contract attempt was rejected, as required; raw failure and diagnostic evidence preserved. The test consumer was corrected, not the guard.

This is synthetic tiny content, not real-decoder/two-hop or target-rate acceptance. CPU layout probing may allocate staging storage; this test does not establish absence of every internal allocation/copy. No production/API/default change in this step. Local native setup still supports explicit CPU readback only. Existing timing/lifetime acceptance failures are not waived.
