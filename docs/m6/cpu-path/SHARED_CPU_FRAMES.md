# Internal CPU ready/release protocol

The explicitly negotiated shared-CPU control now carries FCF1 ready and FCR1
release messages (types 12/13). Each exact 40-byte message contains version,
generation, sequence, PTS, slot and zero reserved bytes; no FDs are permitted.
No legacy protocol layout/default changes. No public factory selects this route.

After pool acceptance, producer submits only completed CPU writes. A per-slot
outstanding sequence blocks repeat submission with FL_BUSY; no slot is silently
dropped or recycled. Ready sequences strictly increase across the connection
(nonzero, no wrap). Consumer validates generation, range, sequence and slot state
before publishing the frame. Nonnegative nanosecond PTS is preserved exactly.
Consumer release requires the exact outstanding slot/sequence and completed reads.
Producer recycles only upon that exact release (release PTS must be zero).

All calls are externally serialized. Malformed messages and peer death close the
connection and clear acceptance, without recycling outstanding slots or destroying
independently retained mappings. Receive outputs remain unchanged on failure.
Socket receive deadlines bound a silent peer; there is no inferred release on
wait timeout. A fresh connection/generation and explicit safe pool replacement
are needed after failure. This is a bounded internal fail-closed primitive, not a
choice of new public overload/default scheduling policy.

Raw Mapping pointers still require caller discipline: this protocol cannot stop a
caller from writing early or retaining a pointer after release. Public integration
must wrap these in actual borrow/lease APIs, account multi-readers, metadata,
receive depth, EOS and reconnect, and reject native GPU APIs for CPU-only storage.
No GPU fence or GPU completion support is claimed. No A/V/rate qualification yet.

## Evidence and remaining checks

Linux shared-CPU/IPC suite 7/7 passes; Android control runtime and both ABI builds
pass. Real forked producer/consumer exchange 32 exact 16x16 BGRA frames using two
slots, verify all bytes and exact sequence/PTS, and release before reuse. Producer
attempts to submit each held slot again and gets FL_BUSY. Consumer sees bounded
EOF on producer exit with its output unchanged. Eleven hostile-ready controls
cover epoch, zero/replayed sequence, invalid slot, reserved/tag/PTS/type/length,
unexpected FD and reuse of a still-held slot; failures preserve output and FD
baseline. Existing setup and ACK negatives remain passing.

Remaining focused protocol tests: hostile release spoof/replay, out-of-order
independent slot releases, multi-slot saturation and death while holding frames.
Then lease API integration and current channel/coordinator/GStreamer routing.
