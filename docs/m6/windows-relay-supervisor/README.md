# Opt-in idle two-session failure supervisor

Base exact `7b8a2741416a59be1fbd66df13a4e88e95bb9bd9` integration collected without rerun:148/148,119 exports and sixteen diagnostics, all exit0.

`samples/session_relay_supervisor.h` adds a reusable, explicit broker-owned monitor for a pre-authenticated compatible two-hop session pair. It pins `SessionRelayContract`, retains both sessions and checks their existing event-liveness/configuration contract every10ms (not a hard realtime bound). On failure it cancels both sessions and invokes one owner callback; it never forwards media, emits EOS/END, closes channels, completes native leases or reconnects. All existing API/wire/default behavior is unchanged. Start/stop/destruction are owner-serialized; callback runs on the monitor and must not throw, block, destroy the object or call stop. Stop joins observation/callback work. Callers stop after verified aggregate drain before normal teardown; stop itself does not establish drain. Unrelated or already-failed pairs reject without cancelling healthy peers. Fresh object required after stop/failure.

## Focused Windows evidence

Initial real bidirectional endpoint-failure2/2 passed. Final focused **7/7 (4.69s)** passed: supervisor lifecycle controls, both supervised directions, existing GAP/normal/explicit-cancel relay and independent automatic pool rejection. No full suite or broad campaign launched.

- Actual downstream bin NULL while no new input is pushed: upstream cancellation automatically propagated in98.440ms; original source observes cancellation0.062ms later.
- Actual original producer bin NULL: downstream cancellation automatically propagated in5.801ms.
- Both controls retain the final native reader; native finish remains BUSY, no invented END/EOS; exact pixels remain accessible after graph teardown, release only after real reader completion.
- Successful GAP relay uses the monitor too: independent4,4,8 boundary/+5ms mapping/37ms A/V/384PCM oracle passes; normal terminal drain causes no failure callback.
- Lifecycle controls reject same-session/missing compatible contract, pre-cancelled peer, second start and start after stop; stopping is joinable/idempotent and does not cancel the healthy peer.

Commands, exact overlays and SHA256 manifests: `<linux-home>/dev/evidence/m6-cpu-path/windows-relay-supervisor-7b8a274`. Final LastTest.log contains detailed outputs, collected without a diagnostic rerun. Loom pre-edit command unavailable on PATH; authoritative task ledger used directly.

This is a supervision component for broker-composed graphs, NOT automatic local discovery/pool composition or reconnect qualification. No public symbol change; API baseline remains7b8a274, not frozen. Windows implementation-first order remains; Linux/Android broad campaigns deferred. M6open/M7hold.
