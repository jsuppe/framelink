# scratch-off-windows-bgra-3840x2160-30-cpu-1800

Runtimeb5236a4 plus gst/test files from3e0cd85, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4 + gst/test from 3e0cd85",
  "started": 1794010880.6531641,
  "label": "scratch-off-windows-bgra-3840x2160-30-cpu-1800",
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
  "ended": 1794012684.6491835
}

Video arrival ms: {"p50": 18.322334, "p95": 19.841267, "p99": 20.4492, "max": 24.040234}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=38.34
consumer.handles.slope.per_min=-0.00
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=98.11
consumer.threads.slope.per_min=-0.00
producer.cpu.percent=13.52
producer.handles.slope.per_min=-0.00
producer.mem.slope.kb_per_min=7.50
producer.threads.slope.per_min=-0.00

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.

CPU-consumer path. Complete54000frames/86400000PCM,zero34ms misses,AVmapping0ns. Positive privatecommit slopes consumer+98.11/producer+7.50KB/min retained; resource qualification remains open.
