# Windows explicit local native setup

Implementation is the commit introducing this document, based on `8583411`.

`fl_gst_session_open_local_native` composes protected current-user discovery,
fresh authenticated session negotiation, exact native-memory policy agreement,
clock mapping, authenticated PCM/events, and native channel creation/attachment.
FAR4 is distinct from legacy FAR1, Unix CPU FAR2 and Windows CPU FAR3 records.
Legacy and CPU readers reject FAR4 and native readers reject other profiles.
Both endpoints agree on the full producer-ordered candidate before allocating a
channel. Input generation is zero (template); output is the negotiated generation
of a freshly identified session. No settings/defaults silently change.

## Bounded supported profile

Windows D3D11 GPU_SAMPLE producer with zero operations, CPU_READ receiver with
explicit COPY|READBACK, permitted by BOTH requirements. Other operation/usage
profiles reject rather than claiming unsupported native output. The returned
policy/channel are applied through existing bin actions; swap endpoints for the
receiver. Upstream allocation continues to require exact same-device completion,
identity and layout checks. This is **not** end-to-end zero-copy.

The worker caller holds all references, does not reconfigure the session during
setup, and uses GCancellable for cancellation. One deadline covers setup I/O;
native platform allocation calls are synchronous, not forcibly interruptible.
This helper does not enable `native-memory=true session=...` automatic bin setup.
The explicit helper plus broker-style bin actions is the tested integration.

## Verification

- Windows **5/5**: native producer, rendezvous, session transport, shared CPU A/V,
  legacy local A/V. Uses exact seven-file overlays on clean `8583411` tree;
  commands and tested source hashes retained. Later header changes are comments
  clarifying synchronous allocation, reference lifetime and negotiated generation.
- Native producer retains four broker-bound A/V cases and adds three real local
  setup cases: both EOS orders and empty stream. Original D3D11 texture identity,
  exact video pixels/PCM, DTS/duration and common **37 ms** A/V offset validated.
  Test is two endpoint threads on Windows, not independent-process decoder proof.
- Ten new setup negatives: nonzero template generation, hidden-copy policy,
  wrong resource/usage, forbidden readback, distinct valid candidates, legacy peer,
  unsupported pool depth, and cancellation while accepting/discovering. All leave
  outputs clear. Existing broker PCM-death case remains covered.
- FAR4 strict record tests cover short/long records, zero port/key, wrong magic,
  mixed profiles, reader isolation and owner exclusion. Existing lifecycle/ACL
  and CPU discovery tests remain in the executable.
- Linux **4/4** shared regressions; Android **3/3** and arm64/x86_64 builds.
  These are shared-change checks, not resumed platform performance campaigns.
- Initial Windows failure is preserved in `initial-mapping-fixture`: local setup
  already installs immutable mapping, but the inherited broker fixture attempted
  to replace it. Corrected fixture uses the existing mapping and checks exact
  relative video/audio offset. No production behavior changed to bypass it.

## Compatibility / remaining work

Public C API change is additive and Windows-only. Existing public types, structs,
function signatures, policy wire and legacy discovery records are unchanged.
`runtime/av_rendezvous.h` is an internal C++ helper, not an app ABI: rebuild its
users together. No cross-version internal helper ABI guarantee is asserted.
Future platform/profile support can add accepted candidates without changing this
function signature; unsupported paths currently fail explicitly. Generic decoder,
native receiver and two-hop integration remain unqualified. D2/D3 decisions,
Windows resource growth and original `548fe19` 1529 ms startup failure remain open.
API is **not frozen**, M6 remains open, M7 maintenance hold remains.

Loom pre-edit briefing was attempted: CLI required removal of unsupported --json,
then failed because chromadb is missing. No subagents or concurrent campaigns.
