# M5a.3a — reservation safety (2026-11-01)

Subtask only. External-pool ownership/adoption and downstream reader fan-out
remain incomplete; M5a.3 is NOT closed by these repairs.

Changes: validate acquired slot/image on begin, exactly one begin/end transaction,
matching nonzero fence value on end, reject duplicate/unmatched submits (including
sync-FD), reject resize while a producer write reservation exists. New
flCancelBuffer returns an unused reservation after caller finishes all access;
it refuses cancellation after begin. Windows removed-device completed-value
sentinel is not treated as all slots free. Existing wire version unchanged.

Windows causal negative: new held-frame resize test failed because a refused
resize re-imported identical storage and reset producer ownership. Preserve the
ring/generation and close only newly duplicated reply handles on no-op/refusal.
Successful replacement now closes old duplicated fence handles. D3D destruction
is deferred: 16 rapid replacements retained driver handles without context flush;
a flush-only diagnostic control allowed cleanup. Production flush is at ring
reconfiguration/reattach boundaries only, NOT added to the per-frame path.

Final tests: Linux8/8 and Windows8/8 (ownership, four resource formats, expanded
pool, reconnect-held and platform fence test). Android tablet7/7 (ownership,
sync-FD, reconnect-held and four resource formats). No skips. arm64/x86_64 API29
builds passed; x86 device runtime not claimed. New test covers forged/wrong image,
double/unmatched submission, exact begin/end values, cancellation, held storage
through20 refused resizes and16 successful replacements. Windows process-handle
count stays within bounded test tolerance after200ms deferred cleanup, without
test-side flush. Sync-FD eventfd test is a deterministic completion surrogate,
not Vulkan sync-file qualification. The pre-fix Windows failure is retained.

The prior sync-FD test resized with an unused acquired buffer. It now requires
FL_BUSY, explicitly cancels, then resizes. This is an intentional safe API
behavior change: acquired borrows may not be invalidated by geometry replacement.
Legacy flBuffer has no generation token: callers still must not reuse an old
borrow after cancellation/submission/reconfiguration. Strong external-pool
lease identity is remaining task3 work, not proven by pointer validation.

No fresh throughput, physical output or external decoder qualification claimed.
