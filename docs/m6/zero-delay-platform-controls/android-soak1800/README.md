# Android zero-configured-delay 30-minute case

Runtime revision: `4497b3b`; RGBA1920x1080 at60fps, native consumer,
CPU-copying producer, pool2/unread1/output queue1, latency0, sync=false,
default measurement-appsink async=true. Raw timing enabled; trace off.
TCL8483A/API35, arm64 API29 build. No concurrent qualification workload.
One mid-run observational load/thermal snapshot reported no thermal throttling.

- Complete:108,000 producer and consumer frames;86,400,000 PCM sample-frames.
- Raw join:108,000 strictly increasing records with constant inferred PTS offset.
  Audio:180,000 contiguous10ms blocks. Inferred join is not independent identity proof.
- All-frame arrival misses at17ms:video0/108,000;audio0/180,000.
- Video arrival p957.83ms,p998.73ms,max13.56ms;audio max5.95ms.
- CPU:producer55.01%,consumer15.41%. No positive sustained RSS/handle/thread
  slope; RSS decreased slightly. Queue pending/unmatched records zero.
- Output has a startup limitation: first audio appsink callback34.77ms late
  versus arrival2.32ms. All-frame raw output must not be confused with the
  steady-state summary (which excludes the first2s). This motivated the
  explicit no-output-preroll control; this run did not use that new control.
- Immediate-output A/V arrival/render differences include producer frame-copy
  cost. Steady render-lateness difference max5.33ms is not a physical-sync
  measurement or a newly approved budget. Old150ms scheduled-output jitter
  budgets do not qualify transport-only output.

The first attempt was interrupted by the gateway tool transport at600seconds;
its orphaned children were stopped after checking their command lines. It is
not a pass. This native-shell rerun ran to EOS and collected complete results.
The earlier60second case with two late video arrivals remains retained and
unattributed; the later passing case does not prove a repair.

Full summary and derived correlation are alongside this file. Raw CSV hashes
are in raw-sha256.json; raw files are retained in the persistent M6 evidence
archive under m6-soak/android-1080p60. These are transport/measurement-appsink
results, not actual GPU-use, display scanout or acoustic output qualification.
