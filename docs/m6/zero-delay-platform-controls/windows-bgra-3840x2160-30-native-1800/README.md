# windows-bgra-3840x2160-30-native-1800

Runtimeb5236a4, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4",
  "started": 1793986937.1086755,
  "label": "windows-bgra-3840x2160-30-native-1800",
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
  "ended": 1793988741.226134
}

Video arrival ms: {"p50": 12.868817, "p95": 14.14395, "p99": 14.763317, "max": 17.712084}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=6.48
consumer.handles.slope.per_min=0.01
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=69.47
consumer.threads.slope.per_min=0.01
producer.cpu.percent=12.85
producer.handles.slope.per_min=0.01
producer.mem.slope.kb_per_min=7.40
producer.threads.slope.per_min=0.01

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.
