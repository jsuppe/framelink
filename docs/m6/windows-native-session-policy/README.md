# Coordinated native producer: bound-policy prerequisite

A coordinated `framelinksink native-memory=true` previously accepted an adopted native channel without any memory-policy binding. The new negative test reproduced this before the fix (negative-tests.log: 4/5 passed, missing-policy assertion failed). Start now rejects a missing policy before track registration or pool exposure; the existing generation guard rejects stale policy. Plain non-coordinated native and shared-CPU behavior/defaults are unchanged.

Windows focused suite **5/5 passed**. The extended D3D11 producer test covers missing/stale policy rejection and a configured video-only session with a bound policy, original-pool allocation, ready fencing, exact pixels/PTS/DTS/duration/generation, foreign-buffer rejection, retained memory after stop, and cancellation/pool limits. This is a trusted broker-configured fixture, NOT authenticated discovery, PCM integration, external texture import, real decoder, or two-hop qualification. M6.9 remains open.

Shared-code regressions: Linux **4/4** native producer/source, coordinator, CPU A/V; Android **2/2** native producer and CPU A/V, both arm64 and x86_64 builds. No deferred performance campaign ran. Build/test scripts, source hashes and the original negative are preserved here. Loom briefing unavailable (`chromadb` missing).

## Compatibility/migration

This tightens only coordinated native producer startup: callers must adopt the authorized producer channel, apply `GstFrameLinkMemoryPolicy` containing both endpoints' evaluated requirements/paths and matching session generation while NULL, install the configured session context, then start. No C symbol/layout/wire version changes. D2 overload and D3 convenience fallback decisions remain unresolved and their defaults are unchanged. This prerequisite does not freeze the API or establish a complete app integration baseline. Next: exact-revision Windows integrated build/API suite, then producer A/V-bin policy/allocation wiring and authenticated native-session negotiation. Keep M6 acceptance gates and M7 hold.
