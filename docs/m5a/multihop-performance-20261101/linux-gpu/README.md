# Linux paced GPU boundary qualification

Four sequential RGBA1920x1080@60 cases with two original allocations,3720 frames
including120 warmup. One/two boundaries and tracing off/on each passed exact
identities/PTS/duration/generation and GPU pixel verification. Rates respectively
60.002739,59.999178,60.002108,59.998521fps. Added-hop p95 CPU-marker-to-arrival
1.593858ms without tracing and1.636169ms with tracing (budget16.667ms).

Trace joins3720/7440; GPU completions3720 per traced case,zero recorder loss.
No relay payload maps or consumer shared-input maps. Private output verification
reads36 bytes/frame; producer CPU initialization is explicit. Trace rate difference
<0.002%; added p95 tracing overhead -0.011878/+0.030433ms (budgets2%,16.667ms).
Detailed distributions and raw compressed records accompany this report.

Separate100ms delayed-reader case:300/300 exact identities,600 joins,300 GPU
completions. Consumer hold maximum100.433ms; producer acquire wait74.245ms,
arrival gap100.535ms. This demonstrates bounded backpressure with pool2, not a
no-jitter performance pass. Final average60.054fps does not erase the injected
stall. No source reuse corruption was observed.

Missing submit, GPU completion and readback accounting negative controls were
rejected by the trace analyzer; missing identity and changedPTS rejected by timing
analyzer. Reports are valid JSON. Binary provenance inBUILD.md.

Scope: this is Linux RGBA with CPU-generated input and EGL fence-retired GPU
consumer. NV12, Windows/Android matched campaigns, simultaneous PCM, resource/
flush accounting and full task13 closure remain pending. No physical scanout,
acoustic synchronization or universal zero-copy/driver-internal-copy claim.
