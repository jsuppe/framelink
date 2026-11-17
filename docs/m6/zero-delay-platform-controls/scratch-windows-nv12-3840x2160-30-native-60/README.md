# scratch-windows-nv12-3840x2160-30-native-60

Runtimeb5236a4 plus gst/test files from3e0cd85, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4 + gst/test from 3e0cd85",
  "started": 1794004737.1551738,
  "label": "scratch-windows-nv12-3840x2160-30-native-60",
  "seconds": 60,
  "budget_ms": 34,
  "process_exit": 0,
  "complete": "yes",
  "consumer.frames": "1800",
  "consumer.samples": "2880000",
  "consumer.queue.accounting": "complete",
  "consumer.video.deadline_misses": "0/1800",
  "consumer.audio.deadline_misses": "0/6000",
  "analysis_exit": 0,
  "ended": 1794004800.2347353
}

Video arrival ms: {"p50": 7.4792, "p95": 8.781234, "p99": 9.184034, "max": 9.683367}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=7.09
consumer.handles.slope.per_min=-16.66
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=-124.95
consumer.threads.slope.per_min=-0.42
producer.cpu.percent=8.98
producer.handles.slope.per_min=-16.88
producer.mem.slope.kb_per_min=-123.20
producer.threads.slope.per_min=-0.42

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.

Short diagnostic only. Original intermittent stall remains unproven as repaired.
