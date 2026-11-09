# Checked PCM relay: terminal rendezvous cancellation

Owner: Evelyn. Windows-first M6 fault-handling repair; not full relay acceptance.

The upstream terminal condition variable cannot wake on downstream cancellation
or replacement. The pre-fix Windows control cancels the downstream session after
50ms while the terminal is withheld. `finish(1000)` incorrectly returned TIMEOUT
at 1,000,104us. The original failure is retained in `pre-fix/diagnostic.log`.

The adapter now checks the complete two-session contract between waits of at most
10ms, using one monotonic caller deadline. Both-side cancellation and downstream
replacement fail closed and permanently stop publication. Zero-timeout polling
and ordinary finite timeout remain retryable; the existing 60s upper bound stays.
No PCM bytes, EOS or timestamp state are fabricated to interrupt a wait.

The isolated terminal controls use an actual drained native END and a local empty
PCM input while intentionally withholding its session terminal. They verify both
cancellation directions, downstream generation replacement, zero-timeout polling,
a 100ms timeout with no downstream terminal, and subsequent successful retry.
The final diagnostic observed 54,326us downstream cancellation, 50,140us upstream
cancellation and 53,888us replacement, all under the frozen 500ms gate.

Final Windows build/diagnostic and **6/6 focused regressions passed** (26.47s),
including authenticated empty/nonempty relay, exact PCM/pixels/timing, GAP rejection,
queue bounds, cancellation and native ownership. `final/status.json` pins source
hashes on the 4761f69 base. Only Windows targets include these sample headers;
no deferred Linux/Android campaign was launched. Public APIs/wires/defaults are
unchanged. A fresh exact-revision Windows integration is the next baseline gate.

Ordered successful audio GAP forwarding, broker-authenticated port binding and
independent-process coordinated A/V remain separate work. M6 open; M7 held.
