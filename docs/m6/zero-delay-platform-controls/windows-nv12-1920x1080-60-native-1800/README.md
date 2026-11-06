# windows-nv12-1920x1080-60-native-1800

Runtimeb5236a4, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4",
  "started": 1793985113.3803253,
  "label": "windows-nv12-1920x1080-60-native-1800",
  "seconds": 1800,
  "budget_ms": 17,
  "process_exit": 0,
  "complete": "yes",
  "consumer.frames": "108000",
  "consumer.samples": "86400000",
  "consumer.queue.accounting": "complete",
  "consumer.video.deadline_misses": "0/108000",
  "consumer.audio.deadline_misses": "0/180000",
  "analysis_exit": 0,
  "ended": 1793986917.7554154
}

Video arrival ms: {"p50": 3.513217, "p95": 4.933484, "p99": 5.20845, "max": 9.581917}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=5.60
consumer.handles.slope.per_min=0.03
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=7.69
consumer.threads.slope.per_min=0.01
producer.cpu.percent=10.34
producer.handles.slope.per_min=-0.03
producer.mem.slope.kb_per_min=5.68
producer.threads.slope.per_min=-0.01

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.
