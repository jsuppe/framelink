# Task 7 partial: shared native allocation pools

Shared bounded pool implementation now covers Linux DMA-BUF, Android AHB and
Windows D3D11. Native callers must explicitly accept platform completion semantics.
No queue prefill or payload copy. Linux affected tests 4/4, Windows 5/5,
Android four executable suites passed; arm64/x86_64 plugin builds passed.

POSIX fixtures acquire via ALLOCATION, reject absent/unknown completion contract,
exhaust two images, recycle an unpublished completed reservation, preserve exact
allocation/metadata, and reject acquisitions after sink shutdown while retained
memory remains valid. Shared Windows negatives retain geometry/config rejection.
Controlled readiness and CPU-generated pixel content do not qualify actual GPU
fences, generic decoders, or throughput. Remote memory agreement remains pending.
