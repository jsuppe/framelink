# Explicit ordered PCM GAP forwarding

Owner: Evelyn. Windows-first M6 relay work; not full M6 acceptance.

`SessionPcmRelay(..., true)` opts into GAP forwarding. The default continues to
reject serialized media events rather than silently discard them. In opted-in
mode, `forward()` READY means one PCM buffer **or one GAP** was queued;
`gapCount()` distinguishes events from the unchanged PCM frame/byte/block counts.
This is a sample-header source-compatible extension, not a new exported ABI,
wire version, or overload/fallback default.

The adapter validates identity running-time mapping, finite signed-range timing,
and continuity with preceding PCM/GAP. It queues the original event through
appsrc's serialized queue, preserving its timestamps and local sequence number.
No PCM is synthesized, discarded, copied, or rebased. Subsequent PCM must start
at the GAP end unless it explicitly declares DISCONT. Invalid timing, unexpected
media events, cancellation and stale sessions still fail closed.

Zero-byte events need a separate bound: one GAP may be queued at appsrc, one may
be in flight downstream, and one may be retained pending retry. A src-pad probe
returns the queue credit when appsrc dequeues the event. Event and probe hold
shared credit state, never a pointer into the adapter's lifetime. PCM keeps its
existing byte budget. TIMEOUT preserves pending data, including during terminal
retry; EOS is enqueued only after inspected events. The broker must separately
bound its input event producer: appsink's max-buffers bounds samples, not events.
This does not claim an adversarial end-to-end event-queue resource bound.

## Checks and evidence

The Windows diagnostic covers leading/midstream GAP, exact PCM and event order,
local seqnums, two queued GAP attempts with output paused, retained retry,
malformed GAP timing, discontinuity, next-PCM continuity and cancellation with a
pending event. Existing default-mode rejection controls remain unchanged.
Authenticated two-hop terminal GAP checks verify exact common-timeline mapping,
one final GAP before EOS, and unchanged PCM/video terminal counts.

An additional pilot asserted that the original GstEvent seqnum was preserved
across GDP itself. It failed: mapped PTS=2000000ns and duration=1000000ns were
correct, but seqnum=5857 rather than the original1701. This is retained in
`final/` and `seqnum-diagnostic/`. The existing GDP event wire does not promise
stable GstEvent seqnums. The final oracle instead verifies seqnum identity across
the relay's own input/output pads and exact ordering/timing across both transports.
No wire change or transport identity guarantee is inferred from this test.

Only Windows targets include this adapter; no deferred Linux/Android campaign
was launched. Broker-authenticated port binding and independent-process checked
A/V remain separate work. Existing startup/lifetime/rate failures remain open.
API not frozen; M6 open; M7 held.

Final `empty-terminal/` build and diagnostic passed; **6/6 focused Windows regressions passed** (26.88s). Both PCM+GAP+EOS and GAP-only empty-video/empty-PCM terminal cases passed. Exact file hashes on the32963c5 base are pinned in `empty-terminal/status.json`. A clean committed-revision integration is the next gate.
