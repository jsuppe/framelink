# scratch-off-windows-bgra-1920x1080-60-cpu-1800

Runtimeb5236a4 plus gst/test files from3e0cd85, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4 + gst/test from 3e0cd85",
  "started": 1794007216.0461776,
  "label": "scratch-off-windows-bgra-1920x1080-60-cpu-1800",
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
  "ended": 1794009020.3802333
}

Video arrival ms: {"p50": 5.944717, "p95": 7.597584, "p99": 8.79175, "max": 11.411184}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=22.55
consumer.handles.slope.per_min=-0.01
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=21.19
consumer.threads.slope.per_min=-0.00
producer.cpu.percent=10.83
producer.handles.slope.per_min=-0.01
producer.mem.slope.kb_per_min=6.49
producer.threads.slope.per_min=-0.00

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.

CPU-consumer path, not native GPU use. Complete108000frames/86400000PCM,zero17ms misses,AVmapping0ns. Positive private-commit slopes+21.19/+6.49KB/min consumer/producer retained for resource qualification.
