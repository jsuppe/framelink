# Windows checked native/PCM relay across independent processes

Base `1fdbaa607a846a2b9be2ac3365a591726014401f`; final/status.json pins
all test/source overlays. Windows build and explicit diagnostic passed;
**7/7 focused CTest regressions passed in 29.61 seconds**, including the new
registered `gst_native_relay_process_win` (30-second outer timeout).

The parent is the final receiver, with independent producer and relay processes.
The relay uses `SessionNativeRelay` and `SessionPcmRelay` against two separately
authenticated sessions (audio/event/native-policy streams); the source clock,
epoch and generation are shared, not independently rebased. Credentials are
fixture-only broker provisioning, not a production broker or discovery protocol.
Only the two original NT texture handles and a notification pipe are inherited
by the relay. A Windows job with kill-on-close contains children, including on
assertion/timeout failure; each normal child exit is checked.

## Verified scope

- 16 exact RGBA64x32 video frames alternate through **both** original D3D11
  allocations, imported in reversed downstream slot order. Kernel-object identity,
  all pixels, PTS, DTS, duration and generation are checked, not inferred from caps.
- 768 exact stereo S16 PCM sample frames (16 blocks), no extra samples/frames.
  Final mapping adds the same1ms to both tracks, preserving37ms A/V separation.
- Every final held native lease prevents producer reuse while the other allocation
  is reserved. Producer reuse resumes only after actual synchronous pixel-readback
  completion and final lease release. The relay never maps/copies video payloads.
- All16 GAP/barrier pairs plus END retain kind/count/PTS/duration. Authenticated
  aggregate PCM/video terminal agrees with actual forwarded counts and last PTS.
- A separate empty three-process stream terminates with zero counts/last PTS=-1.
  Empty-stream success does not claim a measured A/V offset or held-frame test.

The first fixture run failed forwarding because relay initialization preceded
producer attachment (attachment starts a native ring generation). Its raw failures
are retained in pilot/ and pilot-logs/. The corrected fixture establishes an
explicit producer-attached event before relay initialization; no runtime ownership
or generation guard was relaxed. attached/ passed the diagnostic and6 existing
regressions before final/ added alternating-slot coverage and CTest registration.
A preflight refused a premature next launch while attached/ still ran; it performed
no remote mutation or duplicate execution. Retained in guarded-no-overlap/.

## Not qualified by this test

This is broker-composed raw-native publication, not independent-process decoder
GstBuffer reservation/automatic-bin relay, production credential distribution,
GPU-renderer physical output, target-rate/endurance or default-profile resource
acceptance. Source upload and final pixel-oracle readback are explicit fixture
operations; PCM is serialized/copied. Existing fault tests remain separate from
this multi-process nominal/empty/held-reader integration.

No public C symbol, struct, wire version, policy default or runtime implementation
changed. No deferred Linux/Android campaigns were started. Loom context was
attempted but unavailable (missing chromadb). Exact clean integrated verification
is the next gate. D2/D3, original Windows timing/lifetime failures and M6 remain
open; maintain the M7 hold.
