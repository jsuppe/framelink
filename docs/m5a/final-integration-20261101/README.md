# M5a final integration — gate still open

Linux full96/96 passed with no skips, plus the new registered boundary_av_analyzer
1/1 (eight internal controls):97 unique tests. Production Linux/Android unchanged
by the subsequent Windows-only wake repair; no repeat of unaffected suites needed.
Android production src/include/gst/android/samples unchanged since ae9960f signed
18/18 qualification; latest benchmark helper arm64/x86_64 builds also passed.

Initial Windows full suite94/95 passed. buffer_references failed its existing
500ms wake-latency limit: an already-consumed slot's finalizer released its local
reference between the slot scan and a fresh empty() query. The fresh query could
select a full wait on another unfinished remote frame, hiding the reusable slot.

Fix: remember localBlocked from the scan, then poll the local finalizer promptly
without registering repeated GPU completion events. Reference/remote-fence reuse
requirements and existing timeouts/assertions unchanged. Original integration test
passed50 consecutive runs after repair (14.14s), including saturation, local/remote
completion, concurrent finalizer, cancellation and geometry safety. Full Windows
rerun and sustained affected checks remain pending; no merge yet.

Hosted CI earlier failed because buffer_references was incorrectly put in the
CPU-only whitelist although it creates GPU allocations. Kept in hardware suite,
removed from hosted whitelist; corrected41f204e hosted Linux/Windows both passed.
This does NOT cover the later wake-path fix. Final head CI must pass before merge.

Review also reconciled stale adapter/ownership descriptions and preserved upstream
README design notes. Evidence boundaries: native/same-allocation forwarding does
not prove arbitrary decoder support, driver-internal copies or physical output.
