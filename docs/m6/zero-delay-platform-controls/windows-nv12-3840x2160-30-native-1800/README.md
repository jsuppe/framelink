# windows-nv12-3840x2160-30-native-1800

**Verdict: FAIL timing diagnostic, complete delivery.** Six consecutive video arrivals exceeded34ms starting at index42432; maximum159.06ms. Audio had zero misses; both tracks retain the same PTS mapping. No root cause is claimed.

Runtimeb5236a4, isolated Caspar checkout. Pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw. Linux-only mapping repair does not alter Windows runtime.

{
  "state": "finished_recovered",
  "revision": "b5236a4",
  "started": 1793988784.3926952,
  "label": "windows-nv12-3840x2160-30-native-1800",
  "seconds": 1800,
  "budget_ms": 34,
  "controller_exit": null,
  "peer_exit": "0/0",
  "complete": "yes",
  "analysis_exit": 1,
  "arrival_misses": 6,
  "note": "Controller vanished before collection; recovered original peer results, no rerun."
}

Video arrival ms: {"p50": 8.52905, "p95": 9.765784, "p99": 10.300617, "max": 159.06025}
A/V inferred mapping delta ns: 0

consumer.cpu.percent=5.53
consumer.handles.slope.per_min=-0.01
consumer.mem.kind=private
consumer.mem.slope.kb_per_min=69.70
consumer.threads.slope.per_min=-0.00
producer.cpu.percent=14.11
producer.handles.slope.per_min=-0.01
producer.mem.slope.kb_per_min=-60.65
producer.threads.slope.per_min=-0.00

Small resource growth retained; M6.5 resource qualification pending. Producer still uploads/copies; native caps do not prove consumer GPU use or end-to-end zero-copy. No physical-output claim. Raw files and binary hashes in persistent windows-current archive/run.log. Remaining M6 gates open.

At the stall, sampled producer private commit rose from136424KB to152848KB then returned; consumer commit stayed103900KB. Sampling is ~1s and is correlation, not allocation or scheduling attribution.
