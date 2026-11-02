# M5a final integration — gate still open

Linux full96/96 passed with no skips, plus the new registered boundary_av_analyzer
1/1 (eight internal controls):97 unique tests. Production Linux/Android unchanged
by the subsequent Windows-only wake repair; no repeat of unaffected suites needed.
Android production src/include/gst/android unchanged since ae9960f signed
18/18 qualification; latest benchmark helper arm64/x86_64 builds also passed.
The later portable sample EOS-statistics reporting change is separately checked below.

Initial Windows full suite94/95 passed. buffer_references failed its existing
500ms wake-latency limit: an already-consumed slot's finalizer released its local
reference between the slot scan and a fresh empty() query. The fresh query could
select a full wait on another unfinished remote frame, hiding the reusable slot.

Fix: remember localBlocked from the scan, then poll the local finalizer promptly
without registering repeated GPU completion events. Reference/remote-fence reuse
requirements and existing timeouts/assertions unchanged. Original integration test
passed50 consecutive runs after repair (14.14s), including saturation, local/remote
completion, concurrent finalizer, cancellation and geometry safety. Full Windows
rerun passed94/95, exposing a different startup loss (below); no merge yet.

Hosted CI earlier failed because buffer_references was incorrectly put in the
CPU-only whitelist although it creates GPU allocations. Kept in hardware suite,
removed from hosted whitelist; corrected41f204e hosted Linux/Windows both passed.
Hosted Linux/Windows also passed wake-fix ef80d2c. Final head CI must pass before merge.

Review also reconciled stale adapter/ownership descriptions and preserved upstream
README design notes. Evidence boundaries: native/same-allocation forwarding does
not prove arbitrary decoder support, driver-internal copies or physical output.

## Intermittent startup loss — unresolved

The first full-suite rerun after the wake fix received39/40 video frames in the
cancel/restart startup case, with complete38400 PCM sample-frames, first videoPTS0
and final videoend800ms. There were no reported presentation late/overflow drops.
The producer's40 count represented appsrc enqueue success, not native submission.
Original failure and inspection logs are retained alongside this document.

Thirty isolated traced restart runs passed. Five complete startup-policy/cancel/
restart/long-preroll sequences also passed. A full95-case rerun with trace enabled
only for m5_startup passed95/95 (236.75s). These passing retries do not prove a fix
for the original loss; its exact boundary remains unresolved. No nominal count
assertion or timing budget has been relaxed.

Inspection found an observability gap: ProducerFailureWatch's ERROR-only bus
filter discarded the serialized sink EOS statistics. Synthetic producers now log
actual received/submitted/dropped/ring-full counters just like external pipelines.
The startup regression requires40 actual submissions and zero sink drops as well
as40 consumer receipts. This is diagnostic reporting, not a claimed transport fix.

Diagnostic regression: Linux m5_startup passed1/1 (27.94s); Windows passed1/1 (31.29s), including exact synthetic sink accounting and existing cancellation/long-preroll/negative controls.
