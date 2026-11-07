# Windows bounded CPU/native stage captures

Eight sequential five-second NV12/BGRA CPU/native profiles at1080p60 and4K30. All producer/consumer result files report EOS, complete300/150video and240000PCM counts, zero frozen17/34ms diagnostic misses. All32trace footers report zero dropped events and rejected threads. These short traced samples do not replace sustained qualification or resolve the original159ms event.

Controller limitation: Start-Process returned null numeric ExitCode values after WaitForExit. The original exits.json files remain unchanged. Process termination waits succeeded and endpoint result files establish EOS/counts, but numeric OS exit-code evidence is unavailable; do not report eight verified zero exits. The earlier command-length launch failure occurred before any benchmark and was recovered by uploading scripts. Actual scripts and binary hash manifest are preserved.

Mean CPU readback-map: NV12 .542ms1080p/1.624ms4K; BGRA .782/2.639ms. Bounded CPU output allocation ~.001ms. Producer sinkRender NV12 .28–.33ms1080p/~1.09ms4K; BGRA ~.44/~1.55–1.59ms. AcquireBuffer sub-microsecond means; signal_ready~.05–.06ms, flush_ready~.003ms. These CPU submission durations do NOT time actual GPU execution.

Native srcCreate averages near frame cadence because its scope includes waiting for the next frame. Startup waits must not be mistaken for steady delivery stalls. Physical presentation and GPU scheduling remain unmeasured here. Cross-stage per-frame identity joining and controlled scheduling comparisons remain required for M6.5c/M6.6; old failure is not waived.
