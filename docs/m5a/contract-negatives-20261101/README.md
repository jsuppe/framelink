# M5a.11 — contract and negative qualification

Locally validated, not merged. No production protocol change in this task.

New `fl_av_memory_adversarial` uses an independently constructed fixed wire fixture
on an authenticated real loopback stream. Fifteen sequential bounded cases:
truncated/legacy header, wrong role, reserved byte, zero nonce, bad requirements
magic/version, bad path version, multiple resource bits, unknown operations,
zero allocation binding, invalid proof, reflected producer proof, invalid approval,
forged acceptance of a strict-copy-incompatible path, and a successful control.
Every rejected case preserves the caller's output sentinel; every case completes
within three seconds under the fixture's one-second socket deadline. The strict
negative uses a valid transcript HMAC: authentication cannot override policy.
This is not a general network fuzzing or penetration-test claim.

Coverage retained/re-run:

- `memory_contract`: hard vs preferred requirements, strict vs explicit fallback,
  each forbidden operation, resource masks, unknown bits, wire sizes/reserved bytes,
  endian golden vector, unchanged outputs and C11 ABI.
- `memory_boundary`: conflicting consumers, all-participant operation consent,
  endpoint-specific capabilities and bounded 16-participant behavior.
- `av_memory_agreement`: exact allocation binding, session/key mismatch, invalid
  offers, explicit fallback, unchanged outputs (ten peer configurations).
- Linux `broker_memory_extension`: only-one-peer extension support fails before
  media, strict and hidden-copy negatives, binding mismatch; explicit copy succeeds.
- `gst_policy_generation`: legacy renegotiation remains functional; policy-bound
  source/sink changed pools reject before start/delivery/submit; missing native
  reader completion contract rejects and explicit compatible contract succeeds.

Final Linux7/7 tests; Windows6/6; Android tablet6/6, including all15 new cases on
each platform. arm64/x86_64 API29 builds passed. x86 runtime is not claimed.
Initial Android test target reused a nested generator expression incorrectly; direct
session target dependencies fixed the build. Retained build log shows that failure
and successful subsequent build. Final Windows and Linux target builds also passed.

Production cross-app GPU integration, actual driver/layout/fence constraints and
multi-hop throughput remain M5a.12–13, not substituted by these policy tests.
