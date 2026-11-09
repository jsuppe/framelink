# Checked native event wait: both-session cancellation

The upstream event condition variable cannot wake for downstream cancellation or
session replacement. Before the repair, the new Windows negative returned
TIMEOUT after **1002.265 ms** despite downstream cancellation at 50 ms. This
negative is retained in `pre-fix/diagnostic.log`.

`SessionNativeRelay::forwardEvent` now checks both configured sessions between
at-most-10-ms event waits using one monotonic caller deadline. Timeout remains
retryable; cancellation/replacement poisons publication and cancels downstream.
A received event is checked again before forwarding. No forced native lease
retirement, new copies, C symbols, wire formats or default policy changes.

Four Windows controls pass: upstream cancellation, downstream cancellation,
downstream replacement, and finite timeout followed by delayed authenticated
barrier delivery exactly once. Initial/terminal zero-timeout polls are retained.
Cancellation measured 50.526/54.364 ms; replacement 54.967 ms. These are bounded
functional controls, not end-to-end latency qualification. Existing held-reader,
GAP/END/FLUSH, PCM and ownership checks remain active.

The focused Windows suite passed **6/6 in 27.80 s**, build and standalone relay
diagnostic exit0. `final/status.json` pins source hashes over base9d113ef. Only the
Windows diagnostic currently instantiates these sample headers; no deferred
Linux/Android campaign was started. The exact clean integrated revision is the
next gate. Independent-process coordinated A/V remains open; this does not
supersede startup/resource failures or freeze the API. Loom briefing was attempted
but unavailable because chromadb is missing.
