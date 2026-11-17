# Android BGRA1080p60 startup diagnostic — timing FAIL retained

Revision4f37f25, binary1b65521e,1800s. Exit0/pull0,108000video/86400000PCM and180000contiguous audio blocks. Exactly2/108000 video arrivals exceed17ms: index0=21.381308ms,index1=20.317719ms. All later frames meet17ms. p95=9.913489ms. Audio zero misses, inferred A/V mapping delta0ns. RSS/handle/thread slopes approximately0. Analysis exit1 is expected failure, not qualification. No startup exclusion or timing waiver.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-bgra-1920x1080-60-1800-cache-startup-diagnostic

Next causal control: isolate fresh shared-memfd first-write page faults from warm writes at identical full BGRA payload; measure setup separately and retain all samples. Existing shared allocator maps lazily and does not prefault physical pages. This is a hypothesis, not a proven attribution of end-to-end startup latency.
