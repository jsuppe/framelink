# M2 independent review (2026-10-30)

Reviewed baseline `0a5ba51`; code corrections are `fddbf7c`.

Findings and changes:

1. Replacement validation allowed track IDs to change despite their documented
   stable lifetime. It now rejects either changed track ID and tests both cases.
2. Decoding alone cannot establish offer agreement. Added
   `flSessionValidateSelection` to validate configuration syntax and exact
   membership in both actual capabilities. Preference order is a negotiation
   policy, not a reason to reject another mutually supported exact format.
3. Added explicit malformed enum, unselected invalid capability, dimensions/rate
   limits, output-preservation and generation-wrap regression cases. Corrected
   the wire-layout assertion to inspect generation bytes rather than track ID.
4. Added a real C11 consumer target and independently launched contract peers.
   The peer fixture uses fixed offers and harness pipes, verifies identical
   acknowledgement, and rejects unoffered formats, stale generation, changed
   track IDs, absent clock mapping, malformed bytes and modified acknowledgement.
   It implements no media, production offer transport, discovery or authentication.

Validation:

- Linux Release full suite **33/33 passed**, including three session tests.
- Standalone contract test compiled/run with GCC undefined-behavior sanitizer,
  `-fsanitize=undefined -fno-sanitize-recover=all`: passed, no diagnostics.
- The two-process fixture's seven scenarios passed; logs are included.
- Windows and Android final-source qualification are separate evidence owned by
  the platform test agents; these results do not substitute for them.

The codec checks exact length, version, all reserved bytes and signed epoch
range without publishing partial output. Checked clock arithmetic and cumulative
sample-offset division avoid intermediate multiply overflow. M2 is a pure
contract and agreement/rejection gate; no M3 media implementation was started.
