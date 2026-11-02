# Linux GPU matched benchmark provenance

Source46fede2; benchmark binary SHA256
`4a58d715023f60912bf115dd02677b2f2b61dbde0b1200fe4ed711a511fb40e6`.
The build precedes only a comment change in that revision. All final GPU cases
use this identical binary and2 original native RGBA allocations,1920x1080,60Hz,
3720 total frames including120 warmup. Linux RTX3090, EGL native fence consumer.
Source CPU fills original allocations. Relay never maps pixels. Consumer samples
original allocations into a private FBO and retires input leases on native GPU
fences. Verification reads36 bytes/frame from that private output, explicitly
accounted in traces. This is not a zero-work GPU path or physical presentation.
CSV timing alone does not verify GPU completion; companion trace reports do.
No simultaneous PCM is carried; that task13 gate remains open.
