# Checked native-relay PCM and terminal join

`SessionPcmRelay` composes the existing immutable two-session contract with a
broker-bound audio appsink/appsrc pair. The broker still authenticates and binds
the ports; the helper cannot prove that arbitrary caller-supplied ports belong
to a particular session. One serialized reader/writer owns configuration and
forwarding. This is not automatic discovery or independent-process qualification.

The input must have a finite, non-dropping appsink queue; output is nonblocking,
nonleaky, TIME-format appsrc with exact negotiated PCM caps. One pending sample
is retained on downstream queue saturation. Queued output bytes cannot exceed
the downstream negotiated audioQueueMs budget; the pending sample, bounded
input queue and transport/in-flight buffers are separate ownership costs.
TIMEOUT consumes no additional sample on retry. No payload copy occurs in this
helper; the authenticated PCM transport itself still serializes payloads.

Each forwarded sample checks the session snapshot, exact format/rate/channels,
identity TIME segment, valid PTS/duration, nonempty aligned bounded byte count,
sample-rate continuity and timestamp arithmetic. Explicit DISCONT resets only
the continuity checker, not the original timestamps. Buffer metadata/bytes are
forwarded unchanged. Failures cancel the downstream session and permanently
stop this helper. Audio GAP event forwarding is not implemented by this sample
adapter; discontinuous PCM without explicit DISCONT rejects rather than silently
inventing samples or rebasing time. Nonidentity intermediate clocks remain
unsupported. Final-receiver mapping stays independent.

Terminal forwarding requires a fully drained input appsink, no pending PCM,
successfully forwarded authenticated video END and zero outstanding native
relay leases. The upstream authenticated audio terminal's video count/final PTS
must exactly match the actual video relay. Only then is downstream video EOS
accounting published and audio EOS enqueued. This is submission, not a claim of
physical playback completion; the integration test separately waits for final
receiver EOS. Empty A/V follows the same path.

## Checks

Final focused Windows build/diagnostic and **6/6 regressions passed** (commands,
base revision, changed-file hashes, exit codes and raw logs alongside this file).
Earlier successful test iterations are retained under `initial/` and
`queue-controls/`; only the top-level manifest describes the final source.

- Nonempty 16-frame and empty authenticated two-hop A/V, same-allocation video,
  exact 768 PCM frames, original PTS/DTS/duration/generation and 37ms offset.
- PCM duration corruption, missing PTS and misaligned byte count after first-hop
  receive: no PCM forwarded, downstream cancellation, permanent rejection, and
  native BUSY drain until the held reader actually completes.
- Contradictory authenticated terminal video count rejects downstream EOS.
- Deterministic paused-output saturation: retained retry, exactly four delivered
  blocks without duplicates, nonzero time origin and explicit discontinuity.
- Six configuration negatives: blocking/leaky output, dropping/unbounded input,
  wrong output time format and incompatible output PCM caps.
- Prior metadata/session/cancellation, ordered GAP/BARRIER/END and FLUSH native
  lease negatives remain passing.

No public DLL export, wire layout or default changed. Only the Windows diagnostic
includes these sample helpers; no deferred Linux/Android campaigns were resumed.
Independent-process A/V and real-decoder multi-hop, original timing/lifetime
failures and D2/D3 remain open. API not frozen; M6 open; M7 hold.
Loom pre-edit briefing was attempted but unavailable (missing chromadb).
