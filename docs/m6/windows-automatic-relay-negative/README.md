# Automatic native-bin relay boundary: fail closed

Base `3b210e693eeb3fe5880503445179348029e9bc45`; exact test overlays are pinned in status manifests. Windows-only test, no production, public API, wire or default changes.

A first automatic candidate-bound native A/V pair delivers eight exact video frames and PCM with DTS/duration and37ms offset. While retaining its final native reader sample, create a second independently automatic A/V pair. First prove the second pair accepts its own negotiated pool: exact pixels/PCM and37ms offset. Then shallow-copy the first hop reader buffer, preserve the same GstMemory, give it valid second-hop timestamps and queue it to the real second producer bin. Require the specific own-reserved-allocation ERROR within1.5s, not generic negotiation failure or EOS; no second output is allowed. Both second-hop bins must reject reuse. Recheck first-hop pixels and original metadata after teardown, then complete its normal authenticated terminal drain.

Initial targeted1/1 passed (1.04s), foreign-buffer error0.052ms. Final focused10/10 passed (35.30s), foreign-buffer error0.058ms, including post-rejection source pixels/metadata recheck. Both build/test exit0. This is a negative boundary prerequisite, NOT positive automatic relay qualification. The source graph continues after the independent downstream error because automatic graph-level cancellation propagation is not implemented; no claim otherwise.

## Next implementation boundary

`FrameLinkD3DProducer::submit` accepts only its own unpublished producer reservation. Ordinary receiver GstMemory is a retained read lease, not an owned second-hop write reservation. `fl_gst_session_open_local_native_epoch` creates independent pools. Simply relaxing the reservation check would break identity and lifetime guarantees. The existing broker-composed `SessionNativeRelay` remains the positive same-allocation reference; its two rings share immutable external handles and retain upstream ownership until downstream completion.

A positive bin relay therefore needs explicit same-allocation pool composition and a read-lease forwarding adapter, not a silent GPU/CPU copy. Both coordinators must preserve compatible source clock/epoch/generation and invert any intermediate mapping explicitly; two unrelated automatic sessions are not automatically one media timeline. Keep current automatic single-hop APIs compatible; a relay entry point/adapter must be additive and explicit. Next: implement/test this broker-composed bin/lease adapter before automatic local discovery composition, then endpoint cancellation and reconnect. Owner Evelyn; this is internal implementation work, not a request for Jon to approve or an external blocker.

API candidate remains verified3b210e6, NOT FROZEN. D2/D3 unresolved; M6 open/M7 hold. No deferred Linux/Android campaigns were run for this Windows-only test. Loom pre-edit briefing attempted; unavailable (`chromadb` missing).
