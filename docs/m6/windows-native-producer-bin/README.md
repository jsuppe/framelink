# Windows native producer through the A/V bin

`framelinkavsink` adds an opt-in, default-false `native-memory` property and an
`apply-memory-policy(pointer) -> gboolean` action. Its video ghost pad advertises
D3D11Memory only when built with D3D11 support and forwards upstream allocation
queries to the existing native producer pool. Native mode requires a video
track, a broker-adopted channel and accepted memory policy; shared-cpu and
session= discovery are rejected, never silently copied or downgraded.

Order while NULL: select tracks/native-memory, adopt the authorized producer
channel, apply the existing `GstFrameLinkMemoryPolicy` for both endpoints,
install the configured session context, then start. Properties are immutable
once a native channel is adopted; create a fresh bin/session to change them.
The existing completion-contract and encoded upstream memory requirements are
mandatory. Policy freshness and pool-generation checks remain in the inner
sink. Authentication remains the broker's responsibility.

The extended Windows native-producer test exercises the public ghost pad:
original allocation/pool, exact texture pixels, PTS/DTS/duration/generation,
ready fencing, unsupported completion/layout/usage, foreign-buffer rejection,
limited pool and retained buffer after stop. Additional negatives cover absent
and stale policy, native mode without policy even without session context,
audio-only/shared-CPU/discovery configuration. The current positive is a
**video-only coordinated session through the A/V bin**, not native video with
PCM or authenticated native discovery. Those remain next tasks.

No native C symbols/structures, wire protocols or old property defaults change.
This is an additive Windows candidate API, not a frozen app-integration baseline.
Linux/Android native caps are not advertised here yet; their integration must
preserve this contract. D2/D3 unresolved; M6 open, M7 on hold.

Initial MSVC compile rejected preprocessor directives inside a macro argument;
raw negative retained. Moving the optional caps fragment outside the macro fixed
portability. Original Windows timing failure remains open in
`../windows-integrated-548fe19`; startup controls did not waive it.

Validation: Windows **4/4** focused tests, Linux **3/3**, Android **2/2** runtime plus both arm64 and x86_64 builds. Source hashes and exact commands/logs retained. No performance campaign was repeated. Loom pre-edit briefing unavailable because chromadb is missing.
