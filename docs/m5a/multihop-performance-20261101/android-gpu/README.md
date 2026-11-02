# Android warmed GPU boundary qualification

TCL8483A/Mali-G52MC2, RGBA1920x1080@60, two original AHardwareBuffers.
Four sequential cases3720/3720 exact identities/PTS/DTS/duration/generation:
one-/two-hop trace-off60.000591/59.999477fps, trace-on59.999337/60.002044fps.
Added-hop p95 CPU-marker-to-arrival2.037768ms off and2.068923ms on (16.667ms budget).
Traces join3720/7440 boundaries,3720 GPU completions each,zero recorder loss;
relay and GPU consumer shared-input map counts zero. Private36byte/frame
verification readback and producer CPU initialization remain explicit.

Tracing rate difference<0.005%; p95 overhead0.021769/0.052924ms (2%,16.667ms budgets).
Raw records, distributions, traces and provenance included. arm64 and x86_64
API29 benchmark/relay builds passed; original device GPU relay regression16/16.
Only arm64 was runtime-tested here. Same-UID harness, not additional cross-UID proof.

Cold first-use failure and preflight setup correction are documented inBUILD.md.
This is warmed steady-state performance, not cold-start no-drop qualification.
No simultaneous PCM, Android native NV12, driver-internal-copy or physical-output
claim. Broader task13 performance/copy/resource and A/V gates remain open.

Separate100ms reader delay:300/300 identities,600 boundary joins,300 GPU completions. Maximum consumer hold 107.661ms, producer wait 83.979ms, arrival gap 110.898ms. This is fault/backpressure evidence, not nominal-jitter qualification.
