# Windows coordinated native producer lifetime coverage

Base `271c48b80ada9fab9f0cd1b037dd74b03da9f86f`, exact two test-source overlays
hashed in status.json. No production code, API or defaults changed.

The native A/V regression now retains the final producer GstMemory reference
through EOS, NULL transitions, pool deactivation and complete bin/session destruction.
It then verifies the original D3D11 resource identity and usable owning device
before releasing the final reference. This covers broker/local/automatic paths;
empty streams carry no artificial allocation. Existing exact pixels, PCM, 37ms
mapping, terminal ordering and bounded teardown assertions remain active.
Post-teardown identity/device validity is not a post-teardown pixel-readback claim.

Build and focused `gst_native_producer_win` regression passed (10.07 seconds).
The explicit `--lifecycle-diagnostic` option then completed **100/100** fresh
in-process automatic sessions, cycling both EOS orders and empty streams,
with task-cache cleanup before each resource sample. Diagnostic and analysis exit0.
All raw cycles retained. Private bytes first51179520 / last69914624,
range51179520..86421504; all-cycle slope -104.936 KiB/cycle.
Handles first2086 / last1509, range1505..2543, slope-1.4635/cycle.
The cold rise and later cleanup are explicit; negative regression slopes are not
proof of returning to the cold baseline. No resource threshold is silently imposed
or relaxed by this diagnostic. The original persistent-endpoint fresh-device
failures (21.10/84.09 KiB/cycle against16 KiB/cycle) remain unresolved.

This is tiny synthetic content with explicit receiver readback, not target-rate,
real-decoder/native-receiver/two-hop qualification or a lifecycle gate waiver.
Loom pre-edit briefing unavailable: chromadb missing. Source change is Windows-only;
no deferred Linux/Android campaigns resumed. Exact clean integrated build next.
