# Authenticated native-memory agreement

Adds `fl_gst_session_authenticate_native_memory` to the public session transport API. Both endpoints explicitly supply the same producer-ordered candidate. The full encoded session, policy version/generation, both 64-byte requirements and canonical path fields derive a separate HMAC credential; existing fresh directional challenges prove agreement. Host padding is never transmitted/hashed. Rejects invalid versions/reserved fields, incompatible operations and CPU-only resources. Success leaves the dedicated stream owned by the caller; failure closes it and cancels the session. Deadline and caller cancellation are bounded.

This is exact candidate agreement, **not offer selection, protected discovery, native handle transfer or a device/layout probe**. The broker must supply the protected root credential and verify actual allocations/completion contracts. No automatic coordinator publication or element application is implied; callers must apply the verified policy before starting media, swapping endpoints for the consumer. These limitations remain explicit M6.9 work, not a frozen API or a zero-copy claim.

## Verification

- Windows **4/4**: session transport, native producer, pool policy, A/V bin.
- Linux **3/3** shared regressions: session transport, shared CPU A/V, pool policy.
- Android **2/2** shared regressions plus arm64/x86_64 builds.
- C11 public header/signature syntax check passed (`header.c`, `c11-header.log`).
- Transport tests: success plus 19 wrong-key/config/generation/requirements/path/operation/order/version/reserved/domain/role negatives; silent peer deadline and caller cancellation. Existing legacy negotiation/audio/CPU/event tests remain in the same executable.
- Windows native producer A/V now authenticates policy before applying it to the producer bin and receiver video child. Readback is explicitly authorized as COPY|READBACK by **both** endpoints; the upstream allocation request includes that permission. Exact pixels/PCM, common37ms mapping, both EOS orders, empty stream and PCM death pass. Receiver output is CPU memory, not end-to-end zero-copy. Receiver-bin public policy forwarding is still absent (fixture accesses the child); automatic discovery remains rejected.

Commands, source hashes and complete logs are retained here. Windows uses the prior clean 4a49a92 tree with exactly the five hashed files overlaid; this is focused verification, not a clean integrated candidate. Exact clean integration follows the implementation commit.

## Compatibility

Additive symbol only; existing structs, functions, legacy wire layouts and defaults unchanged. Known resource bits permit future Linux/Android adapters without extending this handshake; adapter-specific layout/device/handle agreement is separately required. D2/D3 remain unresolved. No overload or fallback default is inferred. Prior Windows startup1529ms and other suspended acceptance gates remain open; M7 hold.

Loom briefing attempted via installed CLI; unavailable because `chromadb` is missing. No parallel agents or deferred performance campaigns were launched.
