# Automatic native bin cancellation control

Windows native receiver and producer automatic candidate-bound A/V bins. Establish eight exact frames and PCM blocks with 37ms A/V offset, retain last producer and receiver GstMemory across teardown. Stop receiver pipeline via NULL, then queue a producer buffer reserved before shutdown: require ERROR (not EOS) within1.5s. Separately stop producer via NULL without source EOS; require receiver ERROR within1.5s. Both bins remain one-shot; final native resource/device must remain valid through complete destruction.

Pre-fix test failed after3.42s: no ERROR on producer bus after receiver teardown plus next buffer. `fl_gst_session_wait` returned cached readiness without checking the event peer, while retained reader memory kept the native channel alive. No assertion failure was waived. Post-fix first pass receiver107.523ms/producer0.292ms.

Repair shares get_config event-failure handling with wait. Waiting-for-readiness observes event failure at bounded50ms intervals under the original deadline. A ready session checks immediately; no intentional per-frame wait, no new network heartbeat. Authenticated completed local drain remains exempt from peer-liveness checks. No public symbols, wire layout, overload/fallback defaults, or native-lease ownership changed.

Portable tests cover ready and still-waiting producer without a get_config caller. Original Windows failure, manifests, and commands are retained in this directory; fix/ has initial focused and affected regressions. This is not idle producer bus notification, multi-process automatic-bin relay, reconnect reuse, target-rate or lifetime qualification.
