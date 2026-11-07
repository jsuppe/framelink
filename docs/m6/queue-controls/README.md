# Deterministic queue/timing controls (M6.2d)

`test/fl_gst_pool_policy.cpp`, Linux1/1 passed1.74s; Windows1/1 passed4.04s; Android arm64 queue test and retained NV12/RGB10 tests3/3 passed. Windows/Android exact logs attached. Small16x16 fixtures establish semantics; they do not replace1080p60/4K30 qualification.

- **Fast consumer / no wait-to-fill:** pool2 and3, each single frame must arrive before the next frame is even submitted. Exact PTS and all pixel bytes are checked. A fill-before-delivery implementation necessarily times out.
- **Producer gap:** after two deliveries, no submission produces no output; subsequent frames have a large PTS jump but consecutive transport sequence. No invented duplicate or sequence-loss event is allowed.
- **Clock scheduling:** paired sync=false/true output sinks receive an identical future-PTS frame; a pad admission probe confirms transport has already delivered. Unsynchronized output arrives before the target; synchronized output must remain unavailable then arrive at/after target. Arrival and clock wait cannot be conflated.
- **Delayed held reader:** existing pool2/3 controls convert every acquired frame to a read lease. Extra producer frame exercises bounded drop, explicit error and delayed-reader recovery; exact submitted/dropped counts checked. Lease completion permits recovery; held allocations cannot be reused. This is deterministic ownership delay, not evidence of physical GPU completion latency.
- **Burst replacing unread frames:** blocked source pad permits two successive announcements. Expected PTS0 and40ms are delivered,20ms is replaced; exact sequence1→3 gap and missing1 event required. This separates overwritten pending frames from held-reader acquisition failure.
- **Age:** explicit held-source arrival-age expiration does not use future/backward media timestamps as wall-clock age.

Normal source/burst/clock controls leave public overload defaults unchanged. Production renderer GPU fence correctness remains covered by separate native ownership tests, not these synthetic delays. These controls do not explain the original intermittent159ms sustained native stall, nor approve a universal latency budget.
