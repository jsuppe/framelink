# Windows matched native GPU-copy boundary qualification

Eight nominal cases: BGRA/NV12,one/two boundaries,trace off/on. Each3720/3720
identities at59.998–60.002fps,1080p60,pool2,120warmup. Each traced case verifies
3720 GPU completions and3720/7440 boundary joins;zero recorder loss. Added-hop
p95 is~2ms; tracing rate difference<0.004%,p95 differences<0.012ms. Exact reports
and compressed originals included. This is GPU-copy/private-staging verification,
not shader rendering; full private readback and source staging upload explicit.

Separate100ms delayed-reader case initially hit the strict no-gap assertion.
The fault-only harness mode now retains/validates received identities and recovers
to the final frame. It is opt-in and requires an injected delay; nominal mode
still rejects any gap.300 produced,298 received/GPU-completed; missing182,186
are each explained by an actual pending-mailbox replacement.596 boundary joins,
zero trace loss,maximum hold102.5305ms and producer wait78.534ms. This is NOT a
nominal performance pass. A negative control hiding overwrite explanations fails.
No core timeout, ownership or latest-frame policy changed.

Nominal matrix uses binary hash inBUILD.md. Fault-only extension uses hash
11FBB3FC0DFCD37C3376D4268DD737A00E3D3ACE9C553452EA3F3E86D2465F05;
DLL unchanged. Post-extension strict180/180 NV12 smoke passed. Failure traces
are retained alongside the accounted recovery run, not silently discarded.

No simultaneous PCM, physical output, or driver-internal copy qualification.
Task13 remains open for these separately scoped software measurement gates.
