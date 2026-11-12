# Transactional local native relay setup

`samples/local_native_relay_setup.h` combines explicit FAR4 source-relative input, consumer-pool snapshot and FAR5 output setup. Fresh caller-owned sessions; success transfers channels/snapshot/policies atomically. It deliberately never publishes readiness or destroys a running graph. Caller installs media adapters and supervisor, waits for downstream readiness, then releases source readiness; retains snapshot for whole graph and stops writers/drains readers before close.

Failure cancels both sessions, closes only transaction-owned unpublished channels/snapshot and clears empty outputs. Nonempty output resources reject unchanged. One remaining monotonic I/O budget is passed through input/snapshot/output; native driver calls remain synchronous and may exceed it. Reversed handle order preserves allocation-identity checks.

Windows build and5/5 focused tests passed10.17s: real three-process A/V, live replacement, source-death replacement, automatic FAR5 bin, and missing-downstream/explicit-cancellation negatives. Negative source waits for actual local_ready and proves readiness never succeeds. Independent review found no blocking defect. The timing check bounds setup failure but does not independently measure the shared deadline against two full sequential deadlines; budget sharing is additionally established by code review.

No public C API/wire/default change or new global lifetime owner. This completes the identified Windows relay setup implementation gap; exact integrated/API qualification follows. Native timing/resource acceptance and D2/D3 remain open; M6 open/M7 held.
