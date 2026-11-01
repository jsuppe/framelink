# Windows decoder example — M5a.8 example qualification

Caspar: NVIDIA RTX5090, GStreamer1.28.0 d3d11h264dec, synthetic 320x240 H.264.

The probe decoded 30/30 frames into private NV12 arrays: ArraySize9, BindFlags
DECODER, MiscFlags0. NT export failed E_INVALIDARG on all30. Stock decoder source
also uses its own DPB pool, not arbitrary downstream ALLOCATION pools. Therefore
these frames cannot simply be adopted into the current immutable native ring.

Implemented alternative: GPU NV12 -> BGRA color conversion writes directly into
a reserved FrameLink allocation. This is a real format transform, not raw-NV12
zero-copy. The separate process displays received BGRA through a DXGI swapchain.
Both sides wait for actual GPU completion before releasing decoder/read leases.
No application payload CPU map occurs in this path. This is code-path accounting,
not an instrumented proof that all driver/internal copies are absent.

Initial conversion failed because two GstD3D11Device wrappers referenced the same
native device. GStreamer checks wrapper identity. Selecting the exact native-pool
wrapper for decoding and conversion fixed it. Causal control: old wrapper failed
before frame publication; fixed run decoded/transformed/received60/60, zero gaps.

Interactive desktop run: consumer recorded60 receives,60 GPU completions,60
API-accepted presentations, zero gaps, two-image ring. Producer exited0. The
initial PowerShell harness did not retain a reliable consumer ExitCode (blank),
so consumer completion evidence is its final counters, not a claimed exit-code
check. A tightened reusable runner is still required.

The generic consumer's fps figure includes setup/startup and is NOT steady-state
throughput. SSH/session0 cannot create this swapchain; desktop qualification used
an interactive scheduled task. No physical scanout/acoustic/throughput claim.

The reusable runner pins process handles, checks both exit codes and exact final
frame accounting, bounds waits, and stops only children it created. Final GPU and
explicit CPU-readable runs both returned producer0/consumer0 and passed all60
color-bar readback checks. The CPU alternative intentionally downloads decoded
NV12, converts to BGRA on the CPU and uploads through the legacy sink. Declared
operation counts describe this known path, not driver-wide tracing.

The final metadata variant verifies duration for every received frame and carries
PTS/DTS/duration through the native/legacy sinks. See final logs. Negative controls
include unknown mode rejection and the observed unsupported NT-export path; the
existing native sink rejects foreign allocations rather than silently copying.

This validates the bounded example, not raw NV12 stock-decoder pass-through, a
production authenticated A/V session, arbitrary files/codecs/adapters, or sustained
throughput. Later qualification tasks cover multi-hop, counters and performance.
Do not treat this finite example as the milestone performance gate.

All three example targets also build with FRAMELINK_BUILD_TESTS=OFF; sample
packaging does not depend on test targets. This is build-only evidence.
