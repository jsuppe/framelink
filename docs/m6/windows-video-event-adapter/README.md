# Bounded broker video-event adapter

Base exact Windows baseline `abba3eb67a39623376a014f3455b74115707c563` was collected:147/147, API119 exports, fifteen diagnostics all exit0; no repeated campaign.

`samples/session_video_relay_events.h` extracts the prior fixture-only GAP queue into reusable sample C++ code. It composes with the existing `SessionRelayContract`; the broker checks the pinned session contract before draining and again before every actual publication. It does not add exported ABI, wire messages, defaults, discovery, a public factory, or automatic failure supervision.

## Contract

- Observe the already-authenticated native `avsrc` output before its first segment/buffer. One serialized GStreamer stream supplies observations; one broker drains before forwarding each corresponding video frame and after confirming upstream terminal state. Session configuration/caps and native lease compatibility remain broker responsibilities.
- Exactly32 **pending** GAP entries in a recycling ring; arbitrary stream length without accumulating history. Each GAP retains its observed buffer boundary and original PTS/duration. Future-boundary entries stay queued. Skipping an event boundary or overrunning the ring fails closed; nothing is silently dropped.
- Only canonical identity TIME segments are accepted. Rebase, seek/rate/offset/stop changes are rejected, not implicitly translated. Informational TAG events are ignored. Unknown timeline events, replacement stream/caps after media, malformed GAPs, missing segment, invalid buffer PTS and media after EOS produce sticky INVALID.
- FLUSH_START/STOP, explicit broker cancellation and failed publication remain sticky. A send is never retried implicitly. Observations perform no network writes. The observation mutex is released around publication, allowing cancellation while a send is in flight. An already-started send may complete; no subsequent send starts after the adapter sees cancellation.
- EOS observation is not permission to invent terminal success or retire memory. The broker still verifies upstream terminal frame count/last PTS, then sends ordered GAPs, barriers/END and PCM EOS. Native lease release depends only on actual reader completion. Quiesce/remove probes before adapter destruction.

## Controls

The real Windows two-hop bin fixture now uses this adapter while retaining independent upstream/downstream event observers. The oracle checks three GAPs at exact buffer boundaries4,4,8, +5ms final mapping,250/750/500us duration and matching video DISCONT. Explicit PCM DISCONT preserves the37ms A/V offset. Eight frames retain original allocation identity and384 PCM frames remain exact; the final native reader survives upstream/downstream teardown.

The downstream NULL control now explicitly cancels the event adapter on the actual observed session cancellation, rejects further publication, cancels upstream and preserves the outstanding native reader. This is broker-driven supervision, not automatic bin composition.

Direct controls exercise100 recycled event slots; ten invalid cases (zero/unknown/overflow duration, rebase/rate changes, unknown event, queue overflow, skipped boundary, invalid buffer PTS, replacement stream); missing initial segment; FLUSH_START, FLUSH_STOP, explicit and send-result cancellation. Calling status inside the publication callback verifies the observer lock is not held. The real transport oracle is separate from these state-machine controls.

Raw commands, exact source overlays and SHA256 manifest: `<linux-home>/dev/evidence/m6-cpu-path/windows-video-event-adapter-abba3eb`. Initial targeted2/2 passed before expanding actual cancellation integration. Final-overlay affected **12/12 passed (58.90s)**. Three detailed diagnostics (event controls, GAP relay, held-reader cancellation) also exit0. See collected logs. Windows-specific fixture change; no shared production implementation changed, so no deferred Linux/Android campaign is claimed.

M6 remains open, API not frozen, M7 hold. Automatic discovery composition, generalized reconnect and primary-rate qualification remain separate gates.
