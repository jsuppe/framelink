# Local relay replacement and source-death recovery controls

Against881c6f8 plus recorded source hashes. Live replacement focused build+4/4 pass4.87s. Final source-death variant build+3/3 pass4.85s, including live replacement and A/V success. Independent review found no actionable issues.

The old final reader retains its native frame while a complete independent source/relay/final-reader A/V graph runs. Check distinct session and clock IDs, disjoint actual kernel allocations (all old/new pairs), incompatible cross-session relay contract and old pending lease. The old reader re-reads every pixel through its live lease before completing it.

Fault variant terminates the owned old source child with exit73. The supervisor observes failure within1s; no upstream END is accepted and relay videoEnded stays false. A fresh graph delivers8native/384PCM/37ms while old snapshot/reader remain alive. After replacement, the old reader verifies its pixels, really completes its lease and exits. Only then is the failed graph discarded; its pending lease is not fictitiously completed by the relay and no synthetic PCM EOS is accepted.

Local sessions use fresh session/clock identities; each independent session may start media generation1. This is fresh-session isolation, not monotonic generation renumbering or stale-token reuse. No same-path hot replacement, repeated/endurance/reconnect resource gate or default-device memory waiver. Existing Windows memory/timing failures remain unresolved. No production/API/default changes.
