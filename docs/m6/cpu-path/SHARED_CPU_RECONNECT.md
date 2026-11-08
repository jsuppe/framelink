# Public CPU-only reconnect / retained lease check

The public connected-FD API passed 100 sequential fresh generations on Linux and
Android arm64; both Android ABI builds passed. Each cycle creates a fresh delegated
socket and new sealed two-slot BGRA pool with the same session identity and logical
allocation IDs but increasing generation. A forked producer writes a full pattern,
submits sequence 1 and waits for release. Consumer retains two references, closes
while holding the frame, verifies pixels, releases one reference and keeps the
other until the next generation has actually delivered its new pattern. Old/new
mapped pointers are distinct while both are alive. The next pool does not overwrite
retained old pixels. Producer observes disconnect instead of an inferred release.

The child's inherited copy of the old disconnected lease is explicitly dropped
before opening the new channel; it is not counted as a lease of the new peer.
After each cycle, the consumer has exactly one additional FD and one named memfd
VMA for its intentional retained old lease. Final release restores exact FD and
named-VMA baselines. All output sequence/PTS and every logical byte are checked.
This is a public lease/resource correctness test, not RSS-slope qualification,
30-minute throughput, A/V continuity, or recovery of an existing channel in place.

The caller supplies monotonically increasing session generations. A fresh factory
has no knowledge of prior session history: coordinator replacement validation must
still reject stale/replayed generations before opening it. This test does not claim
that an arbitrary fresh socket can authenticate session identity or freshness.

No production correction was required. Evidence and source hashes are in
`shared-cpu-reconnect/`. Next: explicit coordinator/broker selection and media
metadata/EOS integration before GStreamer/A/V rate qualification. Existing Windows
lifecycle failures and M6 acceptance gates are unchanged.
