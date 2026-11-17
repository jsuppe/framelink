# scratch-off-windows-nv12-3840x2160-30-native-1800

Runtimeb5236a4 plus gst/test files from3e0cd85, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished",
  "revision": "b5236a4 + gst/test from 3e0cd85",
  "started": 1794005383.4995446,
  "label": "scratch-off-windows-nv12-3840x2160-30-native-1800",
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
  "ended": 1794007187.562349
}

Video arrival ms: {"p50": 7.4681, "p95": 8.797567, "p99": 9.214167, "max": 12.628367}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=6.81
consumer.handles.slope.per_min=-0.00
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=69.28
consumer.threads.slope.per_min=-0.00
producer.cpu.percent=8.83
producer.handles.slope.per_min=-0.00
producer.mem.slope.kb_per_min=47.13
producer.threads.slope.per_min=-0.00

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.

Observed1800s trace-off pass after scratch reuse:54000frames/86400000PCM,zero34ms misses,AVmapping0ns. Resource slopes remain measured positive (+69.28/+47.13KB/min consumer/producer), not asserted flat. Original six-miss failure remains causally unresolved; this pass demonstrates sustained operation of the changed implementation, not attribution of that earlier event.
