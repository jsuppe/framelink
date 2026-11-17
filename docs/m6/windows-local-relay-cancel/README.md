# Local native relay cancellation

Base c0aa02e; exact changed-file hashes per phase. FAR5 setup now watches the upstream authenticated configuration and identity mapping while waiting for downstream discovery/connection. Cancellation/stale configuration cancels pending setup I/O; the monitor is joined before return and never touches native leases. Legacy independent-pool setup behavior and exported names are unchanged. Native driver/create/open calls remain synchronous platform boundaries.

Focused Windows setup/malformed/cross-process/live controls passed 3/3 (2.60s). A source cancelled 30ms into a 2000ms no-downstream setup returned at32.057ms with no output channel and source slots still held.

Live discovered-session endpoint failure controls passed 3/3 (2.11s), including successful terminal control. Actual downstream NULL reached automatic idle-supervisor cancellation in103.395ms; original producer observed it in0.063ms. Original upstream NULL reached downstream cancellation in5.759ms; original producer observation0.007ms. All eight native frames/384PCM/37ms were checked first. Final native reader remained held through cancellation and both graph teardowns; only actual reader release retired its upstream lease. No END/EOS or successful local drain is invented on failure.

Initial new cancellation tests correctly observed real failure but then entered a fixture branch requiring successful EOS. That failure is retained. Replaced the outdated numeric-mode terminal selection with observed failure state; failed local flows never call successful drain acknowledgment. No production cancellation behavior was changed to satisfy that fixture expectation.

This is focused integration, not full exact-revision Windows qualification, reconnect/lifetime acceptance or general cross-process A/V application qualification. M6 open; M7 hold; API not frozen.
