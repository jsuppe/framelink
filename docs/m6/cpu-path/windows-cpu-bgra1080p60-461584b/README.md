# Shared-CPU BGRA1080p60 frozen461584b — PASS

Missing candidate cell collected2026-11-12T19:51:34.902935Z. Prior76a65d2 result
remains historical; this is not a retry of the failed native two-hop BGRA1080 run.
Exact source461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff, existing pinned Release
binaries; no build or production change.5s preflight passed before1800s launch.
The controller, raw files, commands, config, host/driver and source archive pin
are retained. Windows runtime mutex exclusive; no parallel project work.

-1800s:108000 video frames per role,86400000 PCM sample frames,180000 audio packets.
-Zero17ms video/audio arrival misses. Video max5.263817ms,p953.539584ms;
 audio max3.17085ms,p951.89635ms. Inferred relative A/V mapping delta0ns.
-Memory slopes consumer-0.066858/producer6.609057KiB/min, below64; handles and
 threads meet original1/min and0.5/min limits. CPU reported6.81%/8.83%.
-No outstanding output-queue accounting; both roles EOS and complete counts.
-All37 original remote files match hashes with full collection coverage; all
 uploaded artifacts and four binaries match their pinned values before/after.
-Two independent checker implementations passed remotely; the second raw checker
 rerun locally also passed. Source scope is transport timing/accounting, **not
 payload hashing, physical playback, native identity or a lifecycle waiver**.
-idle-final reports count0 at19:51:34.1933302Z; controller and pull exit0.

Runner adapted the already validated4K adapter to1920x1080,60fps,17ms and checked
against archived NV121080 short data before launch. It retains fixed counts,
strict original gates and no-retry failure handling. No threshold change.

This completes the four frozen461584b shared-CPU format/rate cells only. It does
not freeze the API, qualify later binary hashes, resolve D2/D3, qualify native
BGRA1080 or decoder4K, repair startup/lifetime issues, or close M6/M7.

Original-byte manifest is separate from committed-Git blob hashes. Generated
Python bytecode may be omitted from the archive; original manifest is unchanged.
No binaries/source tar copied. Root byte manifests are not production signatures.
