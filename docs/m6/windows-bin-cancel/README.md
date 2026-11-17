# Automatic native A/V bin cancellation: invalidate cached readiness

Base `40b506a76b98ff3e8ac864dede10fbcd80615603` (production8b057a1); status manifests pin exact tested source overlays. Owner Evelyn. Shared production repair; public signatures, symbols, wire formats and overload/fallback defaults unchanged.

## Reproduction and repair

Two automatic candidate-bound native A/V bins establish eight exact RGBA frames plus PCM, DTS/duration and37ms offset. The final producer and receiver GstMemory remain referenced. Stop the receiver via its actual NULL transition; then queue another producer buffer reserved before cancellation. Acceptance: producer ERROR, not EOS, within1.5s; separately producer NULL without EOS must cause receiver ERROR within1.5s. Verify held native resource/device validity through full teardown and reject reuse of either one-shot bin.

Pre-fix receiver-cancel case failed: **no producer ERROR by3s**, test3.42s. Retained reader memory keeps the native allocation/channel alive; `fl_gst_session_wait` returned cached READY without observing the failed authenticated event peer. No assertion was relaxed. The failed test and original manifest are retained beside this file.

`wait` now uses the same event-failure refresh as `get_config`. Ready sessions check immediately. Not-yet-ready waiters check at bounded50ms intervals under their original deadline, since the event channel does not signal the session condition variable. No new thread, network heartbeat, per-frame sleep or ownership bypass. Authenticated completed-local-drain behavior is preserved. Portable tests exercise both already-ready and blocked5s waiters with peer cancellation and no intervening get_config call.

## Collected validation

- Windows initial focused **2/2 passed**; receiver107.523ms, producer0.292ms.
- Windows affected producer/relay/session/event/local/bin suite **14/14 passed**,66.34s; receiver101.538ms, producer0.304ms. Normal authenticated-drain regressions included.
- Linux rebuilt shared code and plugin: event/session-context/session-transport/AV-bin **4/4 passed**,28.53s. Used existing unprivileged relocated SDK; no broad deferred campaigns.
- Android same shared repair: **3 runtime tests passed** on A06B4AABFB07983; arm64 and x86_64 builds passed. Prior process inventory empty.
- Loom briefing attempted; unavailable due missing chromadb. No delegation.

Raw evidence `<linux-home>/dev/evidence/m6-cpu-path/windows-bin-cancel-20261112`. The earlier clean8b057a1 build was overlaid only after its141/141 full results were archived; those results are not relabeled. Next exact clean Windows integration is separate and not yet claimed.

This is API-operation-triggered failure detection, not idle-producer asynchronous bus notification. Retained resource identity/device validity is not post-teardown pixel readback. No automatic multi-process two-hop bin relay, safe dead-generation reuse, reconnect, resource endurance, target-rate qualification or fullM6 closure is inferred. Those remain open; D2/D3 unresolved, API not frozen, M7hold.
