# Windows broker-composed coordinated native relay

Base `f291ffdbd9eed63db44a2200d816f9b1d11309ac`; exact CMake/test overlays
and SHA256 hashes in status.json. Build, explicit diagnostic and **6/6** focused
Windows regressions pass (22.90 seconds). Registered `gst_native_relay_av_win`
is now part of CTest, not only an opt-in diagnostic.

The implementation composes existing NativeRelay and session/PCM/event APIs:
- Two separately authenticated A/V sessions, distinct session identities and
  credentials, common source clock/epoch. Each binds the exact native policy.
- Two immutable D3D11 rings import the same two allocations in reversed order.
  Canonical NT-object identity, not slot index, chooses the downstream buffer.
- 16/16 original video allocations and 768 exact stereo PCM sample frames pass.
  Original video PTS/DTS/duration/generation survive both native boundaries.
  First PCM receiver preserves running time; only final receiver mapping adds
  1ms to both tracks, preserving the original 37ms video/audio difference.
- Each downstream held lease prevents upstream slot reuse. The spare upstream
  slot is reserved to make this negative deterministic. Final synchronous pixel
  readback completes before the lease is released. No relay payload map/copy.
- All 16 ordered authenticated barriers preserve frame counts and timestamps.
  PCM terminal accounting is verified before forwarding video EOS to hop two.
  Empty two-hop A/V also terminates with exactly zero media and last PTS=-1.

This is an **in-process broker-composition integration**, not automatic bin
relay support, a general relay service, or an independent-process/endurance
qualification. Source upload and final pixel-oracle readback are explicit test
operations; PCM is serialized/copied, not zero-copy audio. Raw video publication
uses the native API, not the producer GstBuffer reservation API. The independent
producer's rejection of imported reader buffers remains unchanged. The existing
three-process native relay regression also passed, but is separate evidence.

Four fixture/build pilots are retained: missing d3d11 test linkage, invalid
same-clock/nonidentity mapping, missing explicit tracks=av, and unsigned empty
last-PTS sentinel. They were corrected without weakening runtime checks. The
video readiness declaration now follows actual ring/relay initialization.
Loom context was attempted and unavailable (missing chromadb).

Next: promote the explicit broker composition into a reusable coordinated relay
adapter with checked cross-session clock/epoch/generation and event/cancellation
handling, then validate independent-process A/V. Automatic local sessions still
allocate independent rings: they must not silently copy or bypass reservations.
No exported APIs, structs, wire versions or defaults changed here. D2/D3 and all
unresolved Windows timing/lifetime acceptance failures remain open. M6 open;
maintenance hold before M7. No deferred Linux/Android campaigns were started.
