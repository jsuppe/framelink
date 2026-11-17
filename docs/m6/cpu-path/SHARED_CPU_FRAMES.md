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

Focused release qualification now passes: `shared_cpu_release` saturates all 16
slots, confirms all reject reuse with BUSY, releases in reverse order and reuses
all slots with newer sequences. Fifteen hostile cases cover tag/version/epoch,
zero or wrong outstanding sequence, nonzero release PTS, invalid slot, reserved
bytes, legacy type, short payload, unsolicited FD, duplicate release, stale release
after resubmission, peer death and silence while slots remain held. Invalid input
preserves receive output and closes acceptance; submitting afterward cannot
recycle a slot. Retained mappings survive destruction of the failed control and
imported pool. Every case returns FD count to baseline. Silent-peer wait uses a
30ms bound and must complete within one second. No production correction was
needed for these cases.

Linux eight shared-CPU/IPC tests pass. Android release test and both ABI builds
pass; raw evidence and source hash are in `shared-cpu-release/`. These are internal
protocol checks, not public channel or GPU lease qualification. Typed public CPU-only borrow/lease integration is now in SHARED_CPU_CHANNEL.md.
Coordinator/GStreamer selection and session lifecycle integration remain pending.
