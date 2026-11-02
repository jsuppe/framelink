# Small pools, bounded wait and receiver queue age

Final Linux7/7 and Windows7/7 affected tests pass: pool policy, original sink-loss
stats, timestamp contract, reader leases and three two-hop timing cases. Android
policy suite passes and both ABI GStreamer builds pass. No runtime skips.

Pool suite exercises nine cases: source capacity2/3, full-ring drop2/3, bounded
error2/3, delayed-reader recovery2/3, and receiver queue expiry. All image slots
are held by read leases to force saturation; release permits reuse without growing
the pool. Drop cases verify received/submitted/dropped/ring-full totals exactly.
Error cases verify RESOURCE_BUSY after bounded wait; recovery cases submit the
waiting frame with its original PTS and no drop. Pool size is immutable when live.

Queue-age fixture blocks the first output buffer, lets a second announcement sit
for150ms, then unblocks. With50ms max-queue-age the second is discarded/counted;
fresh backward PTS remains accepted. It uses local receipt age, never media time.
An initial test probe accidentally blocked stream-start events; corrected to block
BUFFER only so the stale frame genuinely waits behind an acquired first frame.

Properties keep legacy defaults: pool-size4, buffer-wait-ms5,
error-on-ring-full=false, max-queue-age-ms0. Arrival clock capture is opt-in.
Existing error-path drop counters now include no-caps, negotiation, lost channel,
map, invalid PTS and copy failures rather than only tracing those losses.

These are functional bounded-policy checks, not sustained FPS or latency budgets.
Receiver queue age excludes upstream queues, pre-receipt IPC delay, post-acquire
copy/processing and downstream queues. Those require separate instrumentation and
bounded application policies; the option is not an end-to-end latency guarantee.
