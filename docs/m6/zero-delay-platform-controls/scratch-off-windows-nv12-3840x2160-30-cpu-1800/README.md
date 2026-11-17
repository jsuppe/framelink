# Windows NV12 4K30 CPU — 1800 seconds

Runtime b5236a4 plus gst/test files from 3e0cd85 on Caspar; binary hashes in run.log. Pool2/unread1/output1, latency0, transport-only, no-output-preroll, trace off, dual-track raw records.

Complete 54,000 video frames and 86,400,000 PCM sample-frames; 180,000 contiguous audio blocks. No video or audio misses against the frozen 34ms diagnostic. Queue accounting complete; inferred A/V mapping difference 0ns. Video arrival p95 13.804684ms, maximum 16.41665ms.

```
consumer.cpu.percent=20.52
consumer.handles.slope.per_min=-0.01
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=-154.28
consumer.threads.slope.per_min=-0.00
producer.cpu.percent=8.79
producer.handles.slope.per_min=-0.01
producer.mem.kind=private
producer.mem.slope.kb_per_min=46.98
producer.threads.slope.per_min=-0.00
```

All four Windows CPU sustained cases have passed counts/timing. This is not full qualification: earlier memory growth budget failures and the original native 159ms stall remain open. No physical-output or zero-copy claim. Raw CSV files retained in the persistent evidence directory matching this label.
