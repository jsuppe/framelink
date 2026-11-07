# M6.5a — measured software boundary costs and resource evidence

This inventory completes software call-boundary cost measurement, not acceptance of every platform/profile. FrameLink target transport measurements and legacy-renderer presentation measurements are distinct configurations. Raw manifests, revisions and trace-loss checks accompany each evidence directory.

- Android NV12: raw AHBlock/unlock ~.03ms baseline; scalar native-memory normalization dominated. Guarded NV21 bulk rows reduced isolated4Kmap1532ms→17.3ms. In whole-path traces4Knormalization~20.7ms andconsumerzero-fill/copy14.7ms dominate;lock/unlock<.1ms. CPUtargetprofilesstillfail. See android-map/android-nv21-fix/android-cpu-stage-detail.
- Linux NV12: map/unmap<.01ms consumer butzero-fill/readcopy231ms1080p/1148ms4K. Severe frame loss retained. BGRA combined source-create643/474ms includeswaiting/copy, NOT isolatedmemcpy. Native consumer paths retaincomplete shortcounts;producerstillCPUupload. See linux-stage-trace/linux-cpu-stage-trace.
- Windows: CPUreadback-map NV12.542/1.624ms andBGRA.782/2.639ms at1080p/4K;CPUoutputreuseallocation~.001ms. Producerupload+submit sinkRender .28–.44ms1080p/1.09–1.59ms4K. Native source-create includesframearrivalwait;notpureprocessing. See windows-stage-trace,includingnumericOSexitcapturelimitation.
- Presentation: hash-identifiedlegacyWindowsrenderer present-call CPU.303ms mean/.323ms p95,queue-submit CPU.020ms;300unique sourceIDs reachpresentation submission,alltraceszero-loss. Callsincluderepeats. See windows-renderer-present.
- Resourcegrowth is measured in ../zero-delay-platform-controls/RESOURCE_GATES.md and ../lifecycle: selectedsteady1800s captures pass64KB/min after boundedallocationfixes;Windows repeateddevice lifecycle remains21.10/84.09KB/cycle >16 andheldleasecycles82.528 >16. Never substitute shorttraceallocationtimings for sustainedmemorygates.

## Explicit remaining acceptance boundaries

M6.5c must distinguish producer/queue/GPU scheduling;original159msWindowsstallnotrepaired. GPUtimestamps/completeboundaryjoining andtrace-overhead quantification remainM6.6 work. Physicalscanout/audio-device output is not measured by appsinks orAPIreturntimes. Primary native renderer integration remainsM6.9d;legacy100msrendererfixture isnotthatgate. CPU-efficient readback/negotiation andfailedLinux/Androidprofiles remainM6.8. No isolated-hop latency/CPU/GPUproduct budget is approved by this report.
