# Android CPU transport stage attribution

Five-second explicit producer/consumer roles, production70d8407,65536 events per thread per recorder. All eight recorder footers report zero dropped events and rejected threads. Traced runs are diagnostics, not timing qualification. Aggregates include startup and nested scopes; do not sum overlapping scopes or call a residual pure copy time. Unmap includes no-op calls from submit/teardown, so pooled means are not per-frame writeback costs.

1080p: producer sinkRender mean8.05ms, MapPlanes6.20ms, acquire-buffer .086ms; consumer srcCreate16.59ms (includes acquire-frame6.47ms), MapPlanes5.11ms.4K: producer sinkRender33.85ms, MapPlanes21.14ms, acquire-buffer4.59ms; consumer srcCreate39.63ms, MapPlanes21.10ms, acquire-frame.665ms (startup-dominated,p95 .0013ms). Remaining whole CPU path exceeds4K30 frame budget;1080p arrivals also fail17ms diagnostic.

Together with source/local controls and isolated raw locks, mapping/conversion and CPU payload work are dominant measured costs; source generation/local collectors alone do not reproduce the failure. Current public mapping is read/write, requiring normalization on producer map and writeback on consumer unmap even when that caller only needs one direction. Do not remove those operations without a defined access contract. Native-only NV12 output remains unsupported. CPU target repairs/negotiated memory choices belong to M6.8; no unsupported cell is waived.

M6.5b cost isolation is complete. M6.5a still needs finer allocation/copy/lock/convert stage boundaries, other platform scheduling and presentation coverage. Existing scope aggregates cannot distinguish every residual cost.
