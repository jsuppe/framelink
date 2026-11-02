# M5a final integration — merged

## Final gate

PR #7 merged as `3e1d51f` at 2026-11-02 03:14:06 UTC (November 1 EST).
Final source `e38de30` passed full Linux **97/97** (252.39s) and Windows
**95/95** (245.39s), without skips. Logs: [Linux](linux-final-e38.log),
[Windows](windows-final-e38.log). Hosted Linux/Windows checks passed
[run41381322454](https://github.com/jsuppe/framelink/actions/runs/41381322454).
Latest signed Android checks6/6 and dual-ABI builds passed; see producer-catchup.
Post-wake native NV12/BGRA two-hop cases each3720/3720 with full PCM/GPU accounting.
Documentation-only closure follows the merge; no production code differs.

The chronology below preserves failed gates and repairs. Its earlier open-gate
statements are historical, superseded by this final result. The original untraced
39/40 event cannot be retrospectively attributed exactly; controlled matching
producer-stall loss was reproduced and repaired. Physical output remains unqualified.

## Investigation chronology


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

## Sparse receiver diagnostic

With timestamp preservation enabled, framelinksrc posts a
`framelink-source-sequence-gap` element message only when a positive source
sequence gap occurs. Fields: previous-sequence, sequence, missing, pts-ns.
The streaming sample reports it as `video-source-sequence-gap` JSON. This adds
no buffering and no per-frame log; it does not change latest-frame-wins delivery.
Sequence reset/backwards movement is not counted as a positive gap. A configured
source age filter can also cause a gap, so the message alone is not a claim of
pending replacement or an exact explanation for the historical39/40 failure.

A deterministic downstream-pad stall submits three frames, receives sequences1
and3, and verifies exactly one message identifying missing sequence2. Linux and
Windows affected checks passed3/3 each (pool policy, recovery, startup); Android
arm64 device pool-policy suite passed, including this exact sequence assertion.

Four longer traced and four matched untraced synthetic runs each delivered
1000/1000 video frames and960000 PCM sample-frames, with1000 sink submissions and
zero sink drops. They did not reproduce the historical failure and are not a
claimed transport repair or a physical-output qualification.

Android arm64/x86_64 plugin/pool-policy builds and signed cross-app JNI builds also passed for the diagnostic changes. The tablet policy test passed; this does not claim a fresh signed A/V lifecycle matrix.

## Controlled burst repair and final checks

The subsequent [controlled producer-stall investigation](producer-catchup/README.md) reproduced and repaired a matching pending-replacement mechanism on Linux/Windows without changing media timestamps or transport buffering. It does not prove the original uncaptured event had that exact cause. Full final checks and CI follow; no retrospective erasure of the failure. [Post-wake native performance](post-wake-performance/README.md) passed both NV12/BGRA two-hop3720-frame A/V cases with complete identities/PCM.
