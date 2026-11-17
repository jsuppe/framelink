# Checked PCM relay cancellation repair

The previous SessionPcmRelay passed the caller's entire timeout directly to
appsink. Session cancellation does not wake appsink. An empty input therefore
continued waiting after either coordinator was cancelled. The Windows negative
in `pre-fix/diagnostic.log` reproduces failure of the frozen 500ms bound with a
1000ms media timeout and cancellation requested after 50ms.

The helper now uses at most 10ms appsink wait slices against one monotonic
caller deadline, checking the immutable session contract after every slice.
Pending-sample ownership, output backpressure, byte budgets, metadata and
terminal rules are unchanged. Input EOS breaks immediately; zero timeout still
performs a nonblocking pull. The slice is not a scheduling/latency guarantee.

The focused Windows diagnostic checks cancellation of upstream and downstream
sessions, permanent rejection after failure, unchanged 192-frame/four-block
accounting, and a normal empty 100ms timeout (80..500ms measured bound) plus
zero-timeout polling. Existing malformed PCM, bounded retries, exact bytes,
DISCONT, native leases, authenticated events and terminal contradictions remain
covered. Exact source hashes and command/exit records are in status.json/run.py.

No public DLL symbols, layouts, wires or overload/fallback defaults changed.
Only Windows currently includes this sample helper. No deferred platform
campaign resumed. Independent-process checked A/V, audio GAP forwarding,
original Windows timing/lifetime failures and D2/D3 remain unresolved. API not
frozen; M6 open; M7 hold. Loom briefing unavailable: missing chromadb.

Focused Windows build/diagnostic and **6/6** selected regressions passed.
