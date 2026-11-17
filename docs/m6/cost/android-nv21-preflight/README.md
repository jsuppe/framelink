# Post-fix Android NV12 CPU AV target preflights

Exact production revision70d8407, fresh static benchmark;10seconds, pool2/unread1/output1, transport-only/no-preroll, frozen17ms/34ms diagnostic arrival gates. Controller completed collection; BOTH benchmark cases exited1. Neither is qualified.

- 1080p60:599/600video, all480000PCM samples, queue accounting complete;598/599video deadline misses, steady video p95 21.95ms/max26.41ms. Producer pushed600 frames, video push-lateness p95 .63ms. Consumer CPU89.26%, producer65.40%.
- 4K30:295/300video, all480000PCM samples, queue accounting complete;295/295video deadline misses, steady video p95 1583.17ms/max1634.24ms. Producer pushed300frames, video push-lateness p95 1493.42ms. ConsumerCPU131.33%,producer96.92%.

Audio had zero diagnostic misses in both. Source-only and no-transport local-consumer controls are next to distinguish source work from transport mapping/upload/readback. Short failures are retained; no extended qualification run is justified yet. Isolated map acceleration does not remove whole-path frame-budget costs.
