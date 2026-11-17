# trace2-windows-nv12-3840x2160-30-native-1800

Runtimeb5236a4, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4",
  "started": 1794002809.6168385,
  "label": "trace2-windows-nv12-3840x2160-30-native-1800",
  "seconds": 1800,
  "budget_ms": 34,
  "process_exit": 0,
  "complete": "yes",
  "consumer.frames": "54000",
  "consumer.samples": "86400000",
  "consumer.queue.accounting": "complete",
  "consumer.video.deadline_misses": "0/54000",
  "consumer.audio.deadline_misses": "0/180000",
  "analysis_exit": 0,
  "ended": 1794004615.2859228
}

Video arrival ms: {"p50": 8.866684, "p95": 10.47095, "p99": 11.119884, "max": 16.47005}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=5.48
consumer.handles.slope.per_min=-0.01
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=71.39
consumer.threads.slope.per_min=-0.00
producer.cpu.percent=15.91
producer.handles.slope.per_min=-0.01
producer.mem.slope.kb_per_min=34.19
producer.threads.slope.per_min=-0.00

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.

Second full traced non-reproduction, not proof that original six-miss timing failure is repaired. No more identical reruns planned.
