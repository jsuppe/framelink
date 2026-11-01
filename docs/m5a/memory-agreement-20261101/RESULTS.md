# Task 7 partial: authenticated memory-agreement helper

New optional sample-layer control helper, not yet invoked by production sessions.
Existing session ABI and behavior unchanged. Bounded fixed wire messages carry
requirements, endpoint-specific path, trusted resource binding and fresh nonces.
Directional HMAC covers both complete offers and full session configuration;
authenticated acceptance follows both-endpoint policy evaluation.

Linux/Windows affected suites 3/3 each. Android agreement executable passed all
10 scenarios; arm64/x86_64 builds passed. Success and explicitly permitted-copy
fallback pass; mismatched binding/resource, missing usage, forbidden copy, session
and key disagreement, unknown requirement version and missing binding reject.
Failure output remains unchanged. Initial Linux affected invocation lacked two
built executables; after building those targets the full 3/3 suite passed.

The resourceBinding must be independently supplied/verified by a trusted allocator
or broker for the exact pool, allocation identity, generation, layout/device and
completion contract. A random shared label or local handle hash is NOT sufficient.
This helper does not establish those facts or driver compatibility. Production
opt-in extension negotiation and actual native-provider binding remain pending;
no end-to-end strict-mode claim is made from this control test.
