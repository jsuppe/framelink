# Android NV21 production normalization validation

Guarded bulk native row reads/writes and cached chroma swapping replace per-byte uncached NV21 access. Generic YUV fallback, native NV12 aliasing, COPY|CONVERT accounting and required AHB unlock/cache visibility remain unchanged. Writeback consumes scratch only when mapped pointers cease to be valid; each subsequent map replaces all logical bytes.

Both Android ABI builds passed. Nine on-device tests passed: conversion helper, four portable plane-map formats (eight all-byte patterned frames including padded NV12), held leases, normal/dead-producer EGL completion, GStreamer pool policy. Linux helper/layout tests and ASan/UBSan helper run also passed. These are affected tests, not a full integrated suite.

Same public map probe, 20 warm-up + 300 measured iterations, same acquired AHB usage and native NV21 layout:

- 1080p map/unmap means 4.435/0.895 ms, versus baseline 389.366/17.551 ms.
- 4K map/unmap means 17.292/3.335 ms, versus baseline 1531.685/70.169 ms.
- Additional cached copy means 0.567 ms at1080p and2.361 ms at4K in map+copy runs.
- Padded130x66 and content verification passed. Distinct chroma/all-byte verification is in the portable and synthetic tests; the matched timing probe uses neutral chroma to preserve baseline comparability.

No AV throughput or sustained resource qualification follows from these single-buffer measurements. End-to-end CPU target profiles still require validation. setup-failure.json records a recovered controller test-name selection error before any builds/tests; production was not implicated.
