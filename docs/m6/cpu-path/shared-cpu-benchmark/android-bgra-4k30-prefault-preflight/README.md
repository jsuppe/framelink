# Android BGRA4K30 prepared-page preflight — FAIL

555d366/binary8266261c,5s:150video240000PCM exact; first3frames miss34ms at47.206808,43.799706,35.015911ms. p95=24.931527ms; no later misses in this5s sample. CPU59.07/73.60%. No qualification or budget waiver. Short RSS slopes are not endurance evidence.

Matched local-no-transport control150video240000PCM,zero34ms misses,max5.73ms. Initial local command omitted required static argument and failed before media; corrected invocation passed, original error retained. Narrows startup issue to transport path, not isolated copy attribution. Existing physical-page preparation did not resolve this4K startup gate. Owner Evelyn: bounded per-stage first-frame investigation still needed; no blind repeat or changed budget.

Next diagnostic1800s retains every frame/miss, determines whether4K also has later sustained overload versus startup-only failure. It is not acceptance and cannot waive startup. Then use that result to scope targeted copy/consumer-map timing instrumentation. No production edit in this wake.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-bgra-3840x2160-30-5-prefault
