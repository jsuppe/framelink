# Local-consumer resource control

Benchmark-only `--role consumer --local-consumer --result FILE` uses live video/audio test sources into the same queues, appsinks, callbacks, prefaulted per-frame records and resource sampler as the transport consumer. Bin-mode control only; native, element, source-only and lifecycle combinations reject. Normal transport pipelines and production code are unchanged. Exit status requires exact independently calculated video/PCM counts, contiguous PCM timestamps, complete queue accounting and EOS. Scope is explicitly written as `local-consumer-control-no-transport`.

Linux and Windows five-second NV12 4K30 controls each delivered150frames/240000PCM, complete queue accounting. Native-mode misuse rejected on each. Normal transport benchmark smoke passed on both. Windows control video arrival max2.01ms/audio2.35ms. These short checks validate the control, not resource stability or FrameLink transport.

Long control: same Windows workload1800s, latency0, transport-only, no-output-preroll, outputqueue1,34ms diagnostic. Fixed privatecommit threshold64KB/min unchanged. Compare the consumer harness baseline against source-only7.36KB/min and original native-consumer69.28KB/min; allocations differ intentionally because FrameLink/D3D11 is removed. Causal interpretation remains limited to that distinction.
